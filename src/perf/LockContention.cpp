// crucible::perf::LockContention — libbpf binding implementation.
//
// Third per-program facade in the GAPS-004 series (after SenseHub,
// SchedSwitch, PmuSample).  Mirrors SchedSwitch.cpp's loader shape
// almost 1:1 — same 7-step Phase loop, same WriteOnce/Tagged/
// Monotonic/NonMovable wrapper toolkit, same env-var conventions.
//
// Specific differences vs SchedSwitch.cpp (lock_contention.bpf.c
// has a different map shape):
//   • `lock_wait_count` (1-element ARRAY) replaces `cs_count` —
//     same syscall-per-read shape, just a different name.
//   • `lock_timeline` (BPF_F_MMAPABLE) replaces `sched_timeline` —
//     same mmap shape, same TimelineHeader+events[] layout, just
//     TimelineLockEvent (32 B) instead of TimelineSchedEvent (32 B).
//   • NO `our_tids` registration — lock_contention.bpf.c uses
//     is_target() (target_tgid match) for filtering, NOT per-tid
//     lookup.  Phase 5 is therefore omitted.
//   • Two attach points (sys_enter_futex + sys_exit_futex), not one
//     — both must attach for the facade to work (a half-attach
//     would leak wait_start entries).  Failure is hard nullopt.
//
// Cost: each futex syscall pays ~100 ns extra (two tracepoint
// dispatches + two map ops on enter, four on exit including the
// timeline write + count bump).  For lock-contended workloads at
// ~10K futex_wait/sec this is 1-2 ms/sec ≈ 0.1-0.2% CPU.  Default-on
// safe.  See GAPS-004d header for the full cost model.

#include <crucible/perf/LockContention.h>

#include <crucible/safety/Mutation.h>  // safety::WriteOnce / WriteOnceNonNull / Monotonic
#include <crucible/safety/Pinned.h>    // safety::NonMovable<T>
#include <crucible/safety/Tagged.h>    // safety::Tagged<T, Source>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>  // libbpf_get_error
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

#include <inplace_vector>

extern "C" {
extern const unsigned char lock_contention_bpf_bytecode[];
extern const unsigned int  lock_contention_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

namespace source {
    struct Kernel {};
    struct BpfMap {};
}

using Tgid = ::crucible::safety::Tagged<uint32_t, source::Kernel>;
using Fd   = ::crucible::safety::Tagged<int,      source::BpfMap>;
static_assert(sizeof(Tgid) == sizeof(uint32_t));
static_assert(sizeof(Fd)   == sizeof(int));

[[nodiscard]] Tgid current_tgid() noexcept {
    return Tgid{static_cast<uint32_t>(::getpid())};
}
[[nodiscard]] Fd   map_fd(struct bpf_map* m) noexcept {
    return Fd{bpf_map__fd(m)};
}

[[nodiscard]] bool env_true(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] == '1';
}
[[nodiscard]] bool env_true_either(const char* canonical,
                                   const char* legacy) noexcept {
    return env_true(canonical) || env_true(legacy);
}
[[nodiscard]] bool quiet()   noexcept {
    static const bool kQuiet =
        env_true_either("CRUCIBLE_PERF_QUIET", "CRUCIBLE_BENCH_BPF_QUIET");
    return kQuiet;
}
[[nodiscard]] bool verbose() noexcept {
    static const bool kVerbose =
        env_true_either("CRUCIBLE_PERF_VERBOSE", "CRUCIBLE_BENCH_BPF_VERBOSE");
    return kVerbose;
}

int libbpf_log_cb(enum libbpf_print_level, const char* fmt, va_list args) noexcept {
    if (!verbose()) return 0;
    return std::vfprintf(stderr, fmt, args);
}

void install_libbpf_log_cb_once() noexcept {
    static std::once_flag once;
    std::call_once(once, [] { libbpf_set_print(libbpf_log_cb); });
}

[[nodiscard]] struct bpf_map* find_rodata(struct bpf_object* obj) noexcept {
    struct bpf_map* map = nullptr;
    bpf_object__for_each_map(map, obj) {
        const char* n = bpf_map__name(map);
        if (n == nullptr) continue;
        const std::string_view name{n};
        if (name.ends_with(".rodata")) return map;
    }
    return nullptr;
}

[[nodiscard]] bool tracepoint_exists(const char* category_slash_event) noexcept {
    std::string path = "/sys/kernel/tracing/events/";
    path.append(category_slash_event);
    path.append("/id");
    if (::access(path.c_str(), F_OK) == 0) return true;
    path.assign("/sys/kernel/debug/tracing/events/");
    path.append(category_slash_event);
    path.append("/id");
    return ::access(path.c_str(), F_OK) == 0;
}

void disable_unavailable_programs(struct bpf_object* obj) noexcept {
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, obj) {
        const char* sec = bpf_program__section_name(prog);
        if (sec == nullptr) continue;
        static constexpr const char kPrefix[] = "tracepoint/";
        if (std::strncmp(sec, kPrefix, sizeof(kPrefix) - 1) != 0) continue;
        const char* tp = sec + (sizeof(kPrefix) - 1);
        if (!tracepoint_exists(tp)) {
            (void)bpf_program__set_autoload(prog, false);
        }
    }
}

[[nodiscard]] int libbpf_errno(const void* p, int fallback) noexcept {
    const long le = libbpf_get_error(p);
    return le ? static_cast<int>(-le) : fallback;
}

}  // namespace

struct LockContention::State : crucible::safety::NonMovable<LockContention::State> {
    struct bpf_object*                       obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};

    safety::WriteOnceNonNull<volatile uint8_t*> timeline_base{};
    safety::WriteOnce<size_t>                   timeline_len{};

    Fd                                          wait_count_fd{-1};

    safety::Monotonic<size_t>                   attach_fail_cnt{0};

    State() = default;

    ~State() {
        for (struct bpf_link* l : links) if (l != nullptr) bpf_link__destroy(l);
        if (timeline_base.has_value()) {
            ::munmap(const_cast<uint8_t*>(timeline_base.get()),
                     timeline_len.get());
        }
        if (obj != nullptr) bpf_object__close(obj);
    }
};

LockContention::LockContention() noexcept = default;
LockContention::LockContention(LockContention&&) noexcept = default;
LockContention& LockContention::operator=(LockContention&&) noexcept = default;
LockContention::~LockContention() = default;

std::optional<LockContention> LockContention::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr,
                "[crucible::perf] lock_contention unavailable: %s (%s)\n",
                why, std::strerror(err));
        } else {
            std::fprintf(stderr,
                "[crucible::perf] lock_contention unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // ── 1. Parse the embedded ELF ──────────────────────────────────
    struct bpf_object_open_opts opts{};
    opts.sz          = sizeof(opts);
    opts.object_name = "crucible_lock_contention";
    struct bpf_object* obj = bpf_object__open_mem(
        lock_contention_bpf_bytecode,
        static_cast<size_t>(lock_contention_bpf_bytecode_len),
        &opts);
    if (obj == nullptr || libbpf_get_error(obj) != 0) {
        const int e = libbpf_errno(obj, errno);
        state->obj = nullptr;
        report("bpf_object__open_mem failed (corrupt embedded bytecode — rebuild)", e);
        return std::nullopt;
    }
    state->obj = obj;

    // ── 2. Rewrite target_tgid in .rodata to our PID ───────────────
    if (struct bpf_map* rodata = find_rodata(state->obj); rodata != nullptr) {
        size_t vsz = 0;
        const void* current = bpf_map__initial_value(rodata, &vsz);
        if (current != nullptr && vsz >= sizeof(uint32_t)) {
            std::string rewritten(
                static_cast<const char*>(current), vsz);
            const Tgid     tgid     = current_tgid();
            const uint32_t tgid_raw = tgid.value();
            std::memcpy(rewritten.data(), &tgid_raw, sizeof(tgid_raw));
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    // ── 3. Skip programs whose tracepoints aren't on this kernel ──
    disable_unavailable_programs(state->obj);

    // ── 4. Verify, JIT, allocate maps ──────────────────────────────
    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "verifier rejected, missing CAP_BPF, or kernel too old)", -err);
        return std::nullopt;
    }

    // ── 5. (No our_tids registration — this BPF program filters on
    //       target_tgid only, set in Phase 2.)

    // ── 6. Attach every autoload-enabled program ───────────────────
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, state->obj) {
        if (!bpf_program__autoload(prog)) continue;
        struct bpf_link* link = bpf_program__attach(prog);
        const long       lerr = libbpf_get_error(link);
        if (link == nullptr || lerr != 0) {
            state->attach_fail_cnt.bump();
            if (verbose()) {
                const char* sec = bpf_program__section_name(prog);
                std::fprintf(stderr,
                    "[crucible::perf] lock_contention attach failed for %s (%s)\n",
                    sec ? sec : "<anon>",
                    std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }
        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr,
                    "[crucible::perf] lock_contention link capacity exhausted "
                    "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    // Both sys_enter_futex AND sys_exit_futex are required.  A
    // half-attach (only enter, no exit) would leak wait_start map
    // entries to MAX_ENTRIES (65536) and silently lose contention
    // data after that.  Insist on at least 2 links — if only one
    // attached the kernel is misconfigured and we should bail
    // rather than pretend to be observable.
    if (state->links.size() < 2) {
        report("expected 2 tracepoint attachments (sys_enter_futex + "
               "sys_exit_futex), got fewer — kernel missing futex syscall "
               "tracepoints, or partial CAP_BPF rejection");
        return std::nullopt;
    }

    // ── 7. mmap the lock_timeline ring buffer ──────────────────────
    struct bpf_map* timeline_map =
        bpf_object__find_map_by_name(state->obj, "lock_timeline");
    if (timeline_map == nullptr) {
        report("lock_timeline map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const Fd timeline_fd = map_fd(timeline_map);

    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page = static_cast<size_t>(page_l);
    // sizeof(TimelineHeader) + sizeof(events[TIMELINE_CAPACITY])
    //   = 64 + (4096 * 32) = 131136 bytes.  Round to page granularity.
    const size_t bytes          = sizeof(TimelineHeader) +
                                  TIMELINE_CAPACITY * sizeof(TimelineLockEvent);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED,
                                timeline_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of lock_timeline failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)", errno);
        return std::nullopt;
    }
    state->timeline_len.set(mmap_len_bytes);
    state->timeline_base.set(static_cast<volatile uint8_t*>(mmap_address));

    // Capture the lock_wait_count FD for wait_count() lookups.  The
    // map is a 1-element ARRAY map (not mmap-able as currently
    // declared in lock_contention.bpf.c), so reads require a syscall.
    if (struct bpf_map* wc =
            bpf_object__find_map_by_name(state->obj, "lock_wait_count");
        wc != nullptr) {
        state->wait_count_fd = map_fd(wc);
    } else {
        // Soft failure — the timeline still works without
        // lock_wait_count, and wait_count() returns 0 when fd is -1.
        if (verbose()) {
            std::fprintf(stderr,
                "[crucible::perf] lock_contention lock_wait_count map missing — "
                "wait_count() will return 0\n");
        }
    }

    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
            "[crucible::perf] lock_contention partial: %zu program(s) failed to attach "
            "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
            state->attach_fail_cnt.get());
    }

    LockContention h;
    h.state_ = std::move(state);
    return h;
}

uint64_t LockContention::wait_count() const noexcept {
    if (state_ == nullptr || state_->wait_count_fd.value() < 0) return 0;
    const uint32_t key = 0;
    uint64_t       value = 0;
    if (bpf_map_lookup_elem(state_->wait_count_fd.value(), &key, &value) != 0) {
        return 0;
    }
    return value;
}

safety::Borrowed<const TimelineLockEvent, LockContention>
LockContention::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) {
        return safety::Borrowed<const TimelineLockEvent, LockContention>{};
    }
    auto* base = state_->timeline_base.get();
    auto* events = reinterpret_cast<const TimelineLockEvent*>(
        const_cast<const uint8_t*>(base + sizeof(TimelineHeader)));
    return safety::Borrowed<const TimelineLockEvent, LockContention>{
        events, TIMELINE_CAPACITY};
}

uint64_t LockContention::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) return 0;
    auto* base = state_->timeline_base.get();
    auto* hdr  = reinterpret_cast<const volatile TimelineHeader*>(base);
    return hdr->write_idx;
}

safety::Refined<safety::bounded_above<8>, std::size_t>
LockContention::attached_programs() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<8>, std::size_t>
LockContention::attach_failures() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get()
                                 : std::size_t{0}};
}

}  // namespace crucible::perf
