#include <crucible/perf/SchedSwitch.h>

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
extern const unsigned char sched_switch_bpf_bytecode[];
extern const unsigned int sched_switch_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

// These using-declarations sit in an anonymous namespace, whose implicit
// using-directive into the enclosing namespace is what lets the members of
// State below find Fd, Tgid and Tid by name lookup.
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

// The distinct phantom tag makes one facade's ring buffer mapping unusable
// as another facade's mapping at compile time.
namespace {
struct SchedSwitchRingbufTag {};
struct ReadOnlyProt {};
struct SharedShare {};
}  // namespace

struct SchedSwitch::State : crucible::safety::NonMovable<SchedSwitch::State> {
    struct bpf_object* obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};

    using TimelineMmap = ::crucible::safety::OwnedMmap<SchedSwitchRingbufTag, ReadOnlyProt, SharedShare>;
    std::optional<TimelineMmap> timeline_mmap{};

    Fd cs_count_fd{-1};

    safety::Monotonic<size_t> attach_fail_cnt{0};

    State() = default;

    ~State() {
        for (struct bpf_link* l : links)
            if (l != nullptr) bpf_link__destroy(l);
        if (obj != nullptr) bpf_object__close(obj);
    }
};

SchedSwitch::SchedSwitch() noexcept = default;
SchedSwitch::SchedSwitch(SchedSwitch&&) noexcept = default;
SchedSwitch& SchedSwitch::operator=(SchedSwitch&&) noexcept = default;
SchedSwitch::~SchedSwitch() = default;

std::optional<SchedSwitch> SchedSwitch::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr, "[crucible::perf] sched_switch unavailable: %s (%s)\n", why, std::strerror(err));
        } else {
            std::fprintf(stderr, "[crucible::perf] sched_switch unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    struct bpf_object_open_opts opts{};
    opts.sz = sizeof(opts);
    opts.object_name = "crucible_sched_switch";
    struct bpf_object* obj =
        bpf_object__open_mem(sched_switch_bpf_bytecode, static_cast<size_t>(sched_switch_bpf_bytecode_len), &opts);
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

    // The sched_switch tracepoint fires in the context of the previous task,
    // so the BPF program looks up next_pid in this map to recognise one of
    // our own threads switching in.
    if (struct bpf_map* m = bpf_object__find_map_by_name(state->obj, "our_tids"); m != nullptr) {
        const Fd fd = map_fd(m);
        const Tid tid = current_tid();
        const uint8_t one = 1;
        const int fd_raw = fd.value();
        const uint32_t tid_raw = tid.value();
        (void)bpf_map_update_elem(fd_raw, &tid_raw, &one, BPF_ANY);
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
                std::fprintf(stderr, "[crucible::perf] sched_switch attach failed for %s (%s)\n", sec ? sec : "<anon>",
                             std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }
        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr, "[crucible::perf] sched_switch link capacity exhausted "
                                     "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    if (state->links.empty()) {
        report("no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "kernel missing sched_switch tracepoint)");
        return std::nullopt;
    }

    struct bpf_map* timeline_map = bpf_object__find_map_by_name(state->obj, "sched_timeline");
    if (timeline_map == nullptr) {
        report("sched_timeline map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const Fd timeline_fd = map_fd(timeline_map);

    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page = static_cast<size_t>(page_l);
    const size_t bytes = sizeof(TimelineHeader) + TIMELINE_CAPACITY * sizeof(TimelineSchedEvent);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED, timeline_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of sched_timeline failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)",
               errno);
        return std::nullopt;
    }
    state->timeline_mmap.emplace(mmap_address, mmap_len_bytes);

    // The count map is a one-element array map and is not declared
    // mmap-able, so every read of it costs a syscall through this fd.
    if (struct bpf_map* cs = bpf_object__find_map_by_name(state->obj, "cs_count"); cs != nullptr) {
        state->cs_count_fd = map_fd(cs);
    } else {
        if (verbose()) {
            std::fprintf(stderr, "[crucible::perf] sched_switch cs_count map missing — "
                                 "context_switches() will return 0\n");
        }
    }

    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
                     "[crucible::perf] sched_switch partial: %zu program(s) failed to attach "
                     "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
                     state->attach_fail_cnt.get());
    }

    SchedSwitch h;
    h.state_ = std::move(state);
    return h;
}

uint64_t SchedSwitch::context_switches() const noexcept {
    if (state_ == nullptr || state_->cs_count_fd.value() < 0) return 0;
    // The map is a one-element array, so key 0 addresses the single counter
    // that the BPF program adds to on every matching event.
    const uint32_t key = 0;
    uint64_t value = 0;
    if (bpf_map_lookup_elem(state_->cs_count_fd.value(), &key, &value) != 0) {
        return 0;
    }
    return value;
}

safety::Borrowed<const TimelineSchedEvent, SchedSwitch> SchedSwitch::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_mmap) {
        return safety::Borrowed<const TimelineSchedEvent, SchedSwitch>{};
    }
    // The mapped region starts with the header and the events follow it, so
    // the returned view covers only the events.  The mapping is untyped byte
    // storage, so start_lifetime_as_array begins the typed array lifetime
    // inside it.  The bit_cast drops volatile, because libstdc++ has no
    // span<const volatile T> for a non-scalar T: its span instantiates
    // element copy and move constructors, which fail under volatile.  A
    // consumer performs its own atomic load at each field access.
    auto* base = std::bit_cast<volatile uint8_t*>(state_->timeline_mmap->data());
    // The element type stays non-const.  The const-void* overload already
    // returns a const pointer, and a const element type makes libstdc++ emit
    // an asm clobber that writes through a const-qualified array location.
    auto* events = std::start_lifetime_as_array<TimelineSchedEvent>(
        std::bit_cast<const uint8_t*>(base + sizeof(TimelineHeader)), TIMELINE_CAPACITY);
    return safety::Borrowed<const TimelineSchedEvent, SchedSwitch>{events, TIMELINE_CAPACITY};
}

uint64_t SchedSwitch::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_mmap) return 0;
    auto* base = std::bit_cast<volatile uint8_t*>(state_->timeline_mmap->data());
    // The added const selects the overload taking const volatile void*, which
    // returns a const volatile pointer to the header.
    const volatile uint8_t* qbase = base;
    auto* hdr = std::start_lifetime_as<TimelineHeader>(qbase);
    // This volatile load needs no acquire fence.  The BPF program updates
    // write_idx with a barriered add, and ts_ns is the per-event completion
    // marker, so a reader checks ts_ns for a non-zero value before trusting
    // the rest of an event.
    return hdr->write_idx;
}

fixy::wrap::MaxBounded<8, std::size_t> SchedSwitch::attached_programs() const noexcept {
    using R = fixy::wrap::MaxBounded<8, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

fixy::wrap::MaxBounded<8, std::size_t> SchedSwitch::attach_failures() const noexcept {
    using R = fixy::wrap::MaxBounded<8, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get() : std::size_t{0}};
}

SchedSwitch::Snapshot SchedSwitch::snapshot() const noexcept {
    return Snapshot{
        .ctx_switches = context_switches(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
