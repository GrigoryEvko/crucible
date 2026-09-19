#include <crucible/perf/SenseHub.h>

#include <crucible/perf/detail/BpfLoader.h>

#include <crucible/safety/_Mutation.h>
#include <crucible/safety/OwnedMmap.h>
#include <crucible/safety/_Pinned.h>

#include <sys/mman.h>

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <inplace_vector>
#include <optional>

extern "C" {
extern const unsigned char sense_hub_bpf_bytecode[];
extern const unsigned int sense_hub_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

// These using-declarations sit in an anonymous namespace, whose implicit
// using-directive into the enclosing namespace is what carries the names into
// the member-declaration scope of State below.
namespace source = ::crucible::perf::detail::source;
using ::crucible::perf::detail::Tgid;
using ::crucible::perf::detail::Tid;
using ::crucible::perf::detail::Fd;
using ::crucible::perf::detail::current_tgid;
using ::crucible::perf::detail::current_tid;
using ::crucible::perf::detail::map_fd;
using ::crucible::perf::detail::find_rodata;
using ::crucible::perf::detail::disable_unavailable_programs;
using ::crucible::perf::detail::libbpf_errno;
using ::crucible::perf::detail::install_libbpf_log_cb_once;
using ::crucible::perf::detail::quiet;
using ::crucible::perf::detail::verbose;

}  // namespace

struct SenseHub::State : crucible::safety::NonMovable<SenseHub::State> {
    struct bpf_object* obj = nullptr;
    std::inplace_vector<struct bpf_link*, 64> links{};

    // The distinct phantom tag makes one facade's counter mapping unusable
    // as another facade's mapping at compile time.  The protection and
    // sharing types are residency metadata that the mapping wrapper never
    // interprets.
    struct SenseHubCountersTag {};
    struct ReadOnlyProt {};
    struct SharedShare {};
    using CountersMmap = ::crucible::safety::OwnedMmap<SenseHubCountersTag, ReadOnlyProt, SharedShare>;
    std::optional<CountersMmap> counters_mmap{};

    safety::Monotonic<size_t> attach_fail_cnt{0};

    State() = default;

    ~State() {
        for (struct bpf_link* l : links)
            if (l != nullptr) bpf_link__destroy(l);
        if (obj != nullptr) bpf_object__close(obj);
    }
};

SenseHub::SenseHub() noexcept = default;
SenseHub::SenseHub(SenseHub&&) noexcept = default;
SenseHub& SenseHub::operator=(SenseHub&&) noexcept = default;
SenseHub::~SenseHub() = default;

std::optional<SenseHub> SenseHub::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr, "[crucible::perf] BPF sense hub unavailable: %s (%s)\n", why, std::strerror(err));
        } else {
            std::fprintf(stderr, "[crucible::perf] BPF sense hub unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // On failure libbpf either returns null or an error-encoding pointer with
    // its top bit set.  libbpf_get_error reports both, and the pointer is
    // then dropped, because passing an error-encoding pointer to
    // bpf_object__close would dereference the encoded integer.
    struct bpf_object_open_opts opts{};
    opts.sz = sizeof(opts);
    opts.object_name = "crucible_senses";
    struct bpf_object* obj =
        bpf_object__open_mem(sense_hub_bpf_bytecode, static_cast<size_t>(sense_hub_bpf_bytecode_len), &opts);
    if (obj == nullptr || libbpf_get_error(obj) != 0) {
        const int e = libbpf_errno(obj, errno);
        state->obj = nullptr;
        report("bpf_object__open_mem failed (corrupt embedded bytecode — rebuild)", e);
        return std::nullopt;
    }
    state->obj = obj;

    if (struct bpf_map* rodata = find_rodata(state->obj); rodata != nullptr) {
        size_t vsz = 0;
        const void* current = bpf_map__initial_value(rodata, &vsz);
        if (current != nullptr && vsz >= sizeof(uint32_t)) {
            // The section holds a handful of bytes and this is not a hot
            // path, so a std::string copy of it is acceptable.
            std::string rewritten(static_cast<const char*>(current), vsz);
            const Tgid tgid = current_tgid();
            const uint32_t tgid_raw = tgid.value();
            // target_tgid is the only variable in the section, so it sits at
            // offset 0.
            std::memcpy(rewritten.data(), &tgid_raw, sizeof(tgid_raw));
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    disable_unavailable_programs(state->obj);

    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "verifier rejected, missing CAP_BPF, or kernel too old)",
               -err);
        return std::nullopt;
    }

    // The sched_switch tracepoint fires in the context of the previous task,
    // so the BPF program looks up this map to recognise one of our own
    // threads switching in.
    if (struct bpf_map* m = bpf_object__find_map_by_name(state->obj, "our_tids"); m != nullptr) {
        const Fd fd = map_fd(m);
        const Tid tid = current_tid();
        const uint8_t one = 1;
        const int fd_raw = fd.value();
        const uint32_t tid_raw = tid.value();
        (void)bpf_map_update_elem(fd_raw, &tid_raw, &one, BPF_ANY);
    }

    // bpf_program__attach returns a valid pointer, null, or a pointer whose
    // top bit is set encoding a negative errno.  Passing that third form to
    // bpf_link__destroy dereferences the encoded integer, so the
    // libbpf_get_error result gates the push below.
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, state->obj) {
        if (!bpf_program__autoload(prog)) continue;
        struct bpf_link* link = bpf_program__attach(prog);
        const long lerr = libbpf_get_error(link);
        if (link == nullptr || lerr != 0) {
            state->attach_fail_cnt.bump();
            if (verbose()) {
                const char* sec = bpf_program__section_name(prog);
                std::fprintf(stderr, "[crucible::perf] BPF attach failed for %s (%s)\n", sec ? sec : "<anon>",
                             std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }
        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr, "[crucible::perf] BPF link capacity exhausted "
                                     "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    if (state->links.empty()) {
        report("no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "kernel missing every tracepoint, or lacking CAP_PERFMON/CAP_DAC_READ_SEARCH)");
        return std::nullopt;
    }

    struct bpf_map* counters_map = bpf_object__find_map_by_name(state->obj, "counters");
    if (counters_map == nullptr) {
        report("counters map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const Fd counters_fd = map_fd(counters_map);

    // sysconf can return -1 inside a hardened sandbox.  A cast of that to
    // size_t would underflow the page-size bitmask below and yield a garbage
    // mapping length.
    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page = static_cast<size_t>(page_l);
    const size_t bytes = NUM_COUNTERS * sizeof(uint64_t);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED, counters_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of counters map failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)",
               errno);
        return std::nullopt;
    }
    state->counters_mmap.emplace(mmap_address, mmap_len_bytes);

    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
                     "[crucible::perf] BPF sense hub partial: %zu program(s) failed to attach "
                     "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
                     state->attach_fail_cnt.get());
    }

    SenseHub h;
    h.state_ = std::move(state);
    return h;
}

Snapshot SenseHub::read() const noexcept {
    Snapshot snapshot;
    if (state_ == nullptr || !state_->counters_mmap) return snapshot;
    // The BPF producer bumps each counter with a full-barrier add, so an
    // acquire load here makes every kernel-side store that precedes a bump
    // visible, and stops the compiler hoisting later reads above it.
    // __atomic_load_n through a volatile-qualified pointer is a GCC
    // extension.
    const volatile uint64_t* __restrict src = std::bit_cast<const volatile uint64_t*>(state_->counters_mmap->data());
    for (uint32_t i = 0; i < NUM_COUNTERS; ++i) {
        snapshot.counters[i] = __atomic_load_n(&src[i], __ATOMIC_ACQUIRE);
    }
    return snapshot;
}

safety::Borrowed<const volatile uint64_t, SenseHub> SenseHub::counters_view() const noexcept {
    if (state_ == nullptr || !state_->counters_mmap) {
        return safety::Borrowed<const volatile uint64_t, SenseHub>{};
    }
    return safety::Borrowed<const volatile uint64_t, SenseHub>{
        std::bit_cast<volatile uint64_t*>(state_->counters_mmap->data()), NUM_COUNTERS};
}

safety::Refined<safety::bounded_above<64>, std::size_t> SenseHub::attached_programs() const noexcept {
    // The bound holds structurally: links is an inplace_vector of capacity
    // 64, so its size stays in [0, 64].  The Refined wrapper republishes that
    // bound in the type system, and a consumer relies on it without a
    // re-check.
    using R = safety::Refined<safety::bounded_above<64>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<64>, std::size_t> SenseHub::attach_failures() const noexcept {
    // The two counters partition the same program-iteration loop.  Each pass
    // either records a failure here or pushes a link.
    using R = safety::Refined<safety::bounded_above<64>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get() : std::size_t{0}};
}

}  // namespace crucible::perf
