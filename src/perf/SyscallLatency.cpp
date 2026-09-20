#include <crucible/perf/SyscallLatency.h>

#include <crucible/perf/detail/BpfLoader.h>

#include <crucible/safety/_Mutation.h>
#include <crucible/safety/_OwnedMmap.h>
#include <crucible/safety/_Pinned.h>

#include <sys/mman.h>

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include <inplace_vector>
#include <optional>

extern "C" {
extern const unsigned char syscall_latency_bpf_bytecode[];
extern const unsigned int syscall_latency_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

namespace source = ::crucible::perf::detail::source;
using ::crucible::perf::detail::Tgid;
using ::crucible::perf::detail::Tid;
using ::crucible::perf::detail::Fd;
using ::crucible::perf::detail::current_tgid;
using ::crucible::perf::detail::map_fd;
using ::crucible::perf::detail::find_rodata;
using ::crucible::perf::detail::disable_unavailable_programs;
using ::crucible::perf::detail::libbpf_errno;
using ::crucible::perf::detail::install_libbpf_log_cb_once;
using ::crucible::perf::detail::quiet;
using ::crucible::perf::detail::verbose;

}  // namespace

// The distinct phantom tag makes one facade's ring buffer mapping unusable
// as another facade's mapping at compile time.  The protection and sharing
// types are residency-tier metadata that the mapping wrapper never
// interprets.
struct SyscallLatencyRingbufTag {};
struct ReadOnlyProt {};
struct SharedShare {};

struct SyscallLatency::State : crucible::safety::NonMovable<SyscallLatency::State> {
    struct bpf_object* obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};

    using TimelineMmap = ::crucible::safety::OwnedMmap<SyscallLatencyRingbufTag, ReadOnlyProt, SharedShare>;
    std::optional<TimelineMmap> timeline_mmap{};

    Fd total_syscalls_fd{-1};

    safety::Monotonic<size_t> attach_fail_cnt{0};

    State() = default;

    ~State() {
        for (struct bpf_link* l : links)
            if (l != nullptr) bpf_link__destroy(l);
        if (obj != nullptr) bpf_object__close(obj);
    }
};

SyscallLatency::SyscallLatency() noexcept = default;
SyscallLatency::SyscallLatency(SyscallLatency&&) noexcept = default;
SyscallLatency& SyscallLatency::operator=(SyscallLatency&&) noexcept = default;
SyscallLatency::~SyscallLatency() = default;

std::optional<SyscallLatency> SyscallLatency::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr, "[crucible::perf] syscall_latency unavailable: %s (%s)\n", why, std::strerror(err));
        } else {
            std::fprintf(stderr, "[crucible::perf] syscall_latency unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    struct bpf_object_open_opts opts{};
    opts.sz = sizeof(opts);
    opts.object_name = "crucible_syscall_latency";
    struct bpf_object* obj = bpf_object__open_mem(syscall_latency_bpf_bytecode,
                                                  static_cast<size_t>(syscall_latency_bpf_bytecode_len), &opts);
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
            std::string rewritten(static_cast<const char*>(current), vsz);
            const Tgid tgid = current_tgid();
            const uint32_t tgid_raw = tgid.value();
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

    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, state->obj) {
        if (!bpf_program__autoload(prog)) continue;
        struct bpf_link* link = bpf_program__attach(prog);
        const long lerr = libbpf_get_error(link);
        if (link == nullptr || lerr != 0) {
            state->attach_fail_cnt.bump();
            if (verbose()) {
                const char* sec = bpf_program__section_name(prog);
                std::fprintf(stderr, "[crucible::perf] syscall_latency attach failed for %s (%s)\n",
                             sec ? sec : "<anon>", std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }
        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr, "[crucible::perf] syscall_latency link capacity exhausted "
                                     "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    // The attach is all-or-nothing.  With the enter tracepoint attached but
    // the exit one missing, every recorded start entry stays unconsumed and
    // the facade records useless half-events until the LRU hash map evicts
    // them.
    if (state->links.size() < 2) {
        report("expected 2 tracepoint attachments (raw_syscalls/sys_enter + "
               "sys_exit), got fewer — kernel missing raw_syscalls "
               "tracepoints, or partial CAP_BPF rejection");
        return std::nullopt;
    }

    struct bpf_map* timeline_map = bpf_object__find_map_by_name(state->obj, "syscall_timeline");
    if (timeline_map == nullptr) {
        report("syscall_timeline map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const Fd timeline_fd = map_fd(timeline_map);

    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page = static_cast<size_t>(page_l);
    const size_t bytes = sizeof(TimelineHeader) + TIMELINE_CAPACITY * sizeof(TimelineSyscallEvent);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED, timeline_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of syscall_timeline failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)",
               errno);
        return std::nullopt;
    }
    state->timeline_mmap.emplace(mmap_address, mmap_len_bytes);

    if (struct bpf_map* ts = bpf_object__find_map_by_name(state->obj, "total_syscalls"); ts != nullptr) {
        state->total_syscalls_fd = map_fd(ts);
    } else {
        if (verbose()) {
            std::fprintf(stderr, "[crucible::perf] syscall_latency total_syscalls map missing — "
                                 "total_syscalls() will return 0\n");
        }
    }

    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
                     "[crucible::perf] syscall_latency partial: %zu program(s) failed to attach "
                     "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
                     state->attach_fail_cnt.get());
    }

    SyscallLatency h;
    h.state_ = std::move(state);
    return h;
}

uint64_t SyscallLatency::total_syscalls() const noexcept {
    if (state_ == nullptr || state_->total_syscalls_fd.value() < 0) return 0;
    const uint32_t key = 0;
    uint64_t value = 0;
    if (bpf_map_lookup_elem(state_->total_syscalls_fd.value(), &key, &value) != 0) {
        return 0;
    }
    return value;
}

safety::Borrowed<const TimelineSyscallEvent, SyscallLatency> SyscallLatency::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_mmap) {
        return safety::Borrowed<const TimelineSyscallEvent, SyscallLatency>{};
    }
    // The mapping is untyped byte storage, so start_lifetime_as_array begins
    // the typed array lifetime inside it.  The bit_cast drops volatile,
    // because libstdc++ has no span<const volatile T> for a non-scalar T.
    auto* base = std::bit_cast<volatile uint8_t*>(state_->timeline_mmap->data());
    // The element type stays non-const.  The const-void* overload already
    // returns a const pointer, and a const element type makes libstdc++ emit
    // an asm clobber that writes through a const-qualified array location.
    auto* events = std::start_lifetime_as_array<TimelineSyscallEvent>(
        std::bit_cast<const uint8_t*>(base + sizeof(TimelineHeader)), TIMELINE_CAPACITY);
    return safety::Borrowed<const TimelineSyscallEvent, SyscallLatency>{events, TIMELINE_CAPACITY};
}

uint64_t SyscallLatency::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_mmap) return 0;
    auto* base = std::bit_cast<volatile uint8_t*>(state_->timeline_mmap->data());
    // The added const selects the overload taking const volatile void*, which
    // returns a const volatile pointer to the header.
    const volatile uint8_t* qbase = base;
    auto* hdr = std::start_lifetime_as<TimelineHeader>(qbase);
    return hdr->write_idx;
}

safety::Refined<safety::bounded_above<8>, std::size_t> SyscallLatency::attached_programs() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<8>, std::size_t> SyscallLatency::attach_failures() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get() : std::size_t{0}};
}

SyscallLatency::Snapshot SyscallLatency::snapshot() const noexcept {
    return Snapshot{
        .total_syscalls = total_syscalls(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
