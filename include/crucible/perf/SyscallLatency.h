#pragma once

// crucible::perf::SyscallLatency — per-syscall latency drill-down via
// the raw_syscalls/sys_enter + sys_exit tracepoints.
//
// Fourth per-program facade in the GAPS-004 series (after SenseHub,
// SchedSwitch, PmuSample, LockContention).  Where SenseHub aggregates
// `SYSCALL_COUNT` and SchedSwitch attributes off-CPU time to context
// switches, SyscallLatency attributes ON-CPU latency to the SPECIFIC
// syscall number.  Per-syscall_nr aggregation answers questions like:
//
//   "epoll_wait p99 jumped from 12 µs to 480 µs after the kernel
//    update — did sys_enter_futex regress similarly?  No, only
//    epoll_wait's syscall handler is slower."
//
// ─── Two costs, one facade ─────────────────────────────────────────
//
//   total_syscalls() — single bpf_map_lookup_elem syscall, ~1 µs
//                      against the 1-element ARRAY map `total_syscalls`.
//                      Total syscalls recorded for our process since
//                      load().  Cheap enough to call from a per-
//                      iteration loop in tests; NOT for hot paths.
//
//   timeline_view() — Borrowed span over the mmap'd `syscall_timeline`
//                     ring buffer (4096 events × 32 B/event = 128 KB).
//                     Volatile reads of ts_ns identify completed
//                     events (the BPF program writes ts_ns LAST).
//                     Sub-µs per access.
//
// The hash-map drill-down (`syscall_latency` keyed by syscall_nr) is
// NOT exposed in this first facade — it requires hash-map iteration
// syscalls (BPF_MAP_GET_NEXT_KEY + BPF_MAP_LOOKUP_ELEM, ~1 µs per
// entry × ≤300 entries) which is a different cost class and a
// different API shape.  When GAPS-004x extracts the shared
// BpfLoader, the hash-map iteration helper will surface as a shared
// primitive in include/crucible/perf/detail/BpfLoader.h.
//
// ─── Production usage ──────────────────────────────────────────────
//
//     if (auto h = crucible::perf::SyscallLatency::load(crucible::effects::Init{})) {
//         const uint64_t syscalls_pre = h->total_syscalls();
//         // ... run workload ...
//         const uint64_t syscalls_delta = h->total_syscalls() - syscalls_pre;
//
//         // Walk the timeline for the slowest syscalls:
//         const auto timeline = h->timeline_view();
//         const uint64_t write_idx = h->timeline_write_index();
//         // Reader logic per CLAUDE.md ring-buffer convention —
//         // walk write_idx backwards N events; trust each slot when
//         // ts_ns != 0, then sort by duration_ns to find the worst.
//     }
//
// If BPF is unavailable (no CAP_BPF / CAP_PERFMON, kernel too old,
// verifier rejects), load() returns nullopt and the caller proceeds
// with no per-syscall drill-down.  All accessors on a moved-from /
// un-loaded SyscallLatency return 0 / empty Borrowed / Refined{0}.
//
// ─── Known limits ──────────────────────────────────────────────────
//
// (1) **Raw syscall tracepoints only.**  This program traces
//     raw_syscalls:sys_enter / sys_exit which fire for ALL syscalls
//     (cheap dispatch).  Per-syscall semantic context (e.g. WHICH
//     fd was being read, WHICH path was being opened) is NOT
//     captured — that requires per-syscall syscalls:sys_enter_<name>
//     tracepoints (300+ syscalls × dispatch overhead).  See planned
//     GAPS-004f for the BTF-driven variant that gives per-syscall
//     semantic args.
//
// (2) **min_ns / max_ns approximate under concurrent writes.**
//     The BPF program's `if (delta > stats->max_ns) stats->max_ns
//     = delta;` and the analogous min_ns update are not atomic;
//     concurrent writers can race and a smaller delta may overwrite
//     a true max (and vice-versa for min).  A CAS loop would cost
//     ~50-100 ns per syscall-completion event, which on a busy
//     system (1M syscalls/sec) translates to 5-10% CPU overhead.
//     Accepted as approximation in exchange for the BPF cost
//     budget.  Reported via syscall_latency map iteration (future API).
//
// (3) **Timeline ring-buffer slot reuse races on wrap.**  Same
//     limit as SchedSwitch / LockContention — the 4096-event ring
//     wraps every ~4 ms on a busy syscall-heavy workload (1M/sec
//     ÷ 4096 ≈ 244 wraps/sec).  Two cores both incrementing
//     write_idx get DIFFERENT idx values (good), but `idx & MASK`
//     can collide on the same slot after wrap if Core A's BPF
//     program is preempted between bump and write.  In practice
//     BPF runs with preemption disabled, so the window is
//     microseconds.  Documented limit; canonical fix is
//     BPF_MAP_TYPE_RINGBUF (kernel-side producer arbitration).
//     Out of scope for this facade.
//
// FIXED at GAPS-004e ship time (2026-05-04) — applied UPFRONT
// rather than as a follow-up audit round (the audit-round-1
// inheritance bug GAPS-004d had):
// — `syscall_start` map is `BPF_MAP_TYPE_LRU_HASH` (not `HASH`);
//   orphaned entries auto-evict, no MAX_ENTRIES silent-rejection.
// — Compiler barrier (`__asm__ __volatile__ ("" ::: "memory")`)
//   inserted before the `ts_ns` store so clang -O2 cannot reorder
//   the field stores past the completion marker.  Pairs with the
//   userspace reader's `__atomic_load_n(&ts_ns, ACQUIRE)` —
//   "ts_ns LAST as completion marker" is an enforced contract.
// — Single `bpf_ktime_get_ns()` call per event (was two — one for
//   delta, one for ts_ns).  Saves one helper call (~50 ns/event).
// — `TimelineSyscallEvent` padded to 32 B (was 24 B) for cache-
//   line coresidence.  32 divides 64 evenly so each slot in the
//   `events[TIMELINE_CAPACITY]` array sits cleanly within a single
//   64-byte cache line.  Without this pad ~25% of slots straddle
//   two cache lines, opening the torn-read window where the reader
//   observes ts_ns committed while duration_ns/tid/syscall_nr are
//   still in a stale cache line.  Same fix class as
//   `TimelineSchedEvent` GAPS-004b-AUDIT and `TimelineLockEvent`
//   at GAPS-004d ship time.

#include <crucible/effects/Capabilities.h>  // effects::Init capability tag
#include <crucible/safety/Borrowed.h>       // safety::Borrowed<T, Source>
#include <crucible/safety/Refined.h>        // safety::Refined / bounded_above

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

// Mirrors `struct timeline_syscall_event` in
// include/crucible/perf/bpf/common.h.  The BPF program writes
// fields in the order [duration_ns, tid, syscall_nr, ts_ns] with
// ts_ns LAST; readers MUST check ts_ns != 0 before trusting the
// other fields (incomplete writes are visible until ts_ns lands).
//
// 32 bytes = duration_ns(8) + tid(4) + syscall_nr(4) + ts_ns(8) +
//            _pad(8).  32 divides 64 (cache line size) evenly, so
// each slot in the `events[TIMELINE_CAPACITY]` array sits cleanly
// within a single cache line — eliminates the torn-read window
// where the reader observes ts_ns committed (in second cache line)
// while duration_ns/tid/syscall_nr are still in a stale cache line
// (first cache line).  Same fix class as `TimelineSchedEvent`
// GAPS-004b-AUDIT and `TimelineLockEvent` at GAPS-004d ship time.
struct TimelineSyscallEvent {
    uint64_t duration_ns;  //  8 B  syscall_exit_ts - syscall_enter_ts (ns)
    uint32_t tid;          //  4 B  thread that issued the syscall
    uint32_t syscall_nr;   //  4 B  syscall number (e.g. SYS_read=0, SYS_write=1)
    uint64_t ts_ns;        //  8 B  bpf_ktime_get_ns() — WRITTEN LAST
    uint64_t _pad;         //  8 B  cache-line-coresidence pad
};
static_assert(sizeof(TimelineSyscallEvent) == 32,
    "TimelineSyscallEvent must be 32 B (duration_ns 8 + tid 4 + "
    "syscall_nr 4 + ts_ns 8 + _pad 8) to match the BPF program's "
    "struct timeline_syscall_event in common.h — wire contract with "
    "BPF_F_MMAPABLE map.  32 divides 64 evenly, so events[N] never "
    "spans two cache lines.");

}  // namespace crucible::perf

// TimelineHeader + TIMELINE_CAPACITY + TIMELINE_MASK live in
// SchedSwitch.h (the first per-program facade to ship them) —
// see the corresponding comment in LockContention.h.  GAPS-004x
// (BpfLoader extraction) will move these into a sibling
// include/crucible/perf/Timeline.h header.
#include <crucible/perf/SchedSwitch.h>  // for TimelineHeader, TIMELINE_CAPACITY

namespace crucible::perf {

class SyscallLatency {
 public:
    // ─── Snapshot — consumer-shaped delta semantics ──────────────────
    //
    // Same shape as SchedSwitch::Snapshot.  `delta.total_syscalls` =
    // syscalls completed in window; `delta.timeline_index` = events
    // produced into the ring.
    struct Snapshot {
        uint64_t total_syscalls = 0;
        uint64_t timeline_index = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            Snapshot r;
            if (__builtin_sub_overflow(total_syscalls, older.total_syscalls,
                                        &r.total_syscalls)) [[unlikely]] {
                r.total_syscalls = 0;
            }
            if (__builtin_sub_overflow(timeline_index, older.timeline_index,
                                        &r.timeline_index)) [[unlikely]] {
                r.timeline_index = 0;
            }
            return r;
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Load the embedded BPF program (syscall_latency.bpf.c), set
    // target_tgid to getpid(), attach the raw_syscalls/sys_enter +
    // sys_exit tracepoints, mmap the syscall_timeline.  Returns
    // std::nullopt if any step fails (missing CAP_BPF / CAP_PERFMON,
    // kernel lacks the syscalls tracepoints, verifier rejects, or
    // fewer than 2 programs attach).
    //
    // The first parameter is a `::crucible::effects::Init`
    // capability tag (1 byte, EBO-collapsed at most call sites).
    // Same gate as SenseHub::load / SchedSwitch::load — hot-path and
    // background frames hold no Init token, so the cap-typing gate
    // prevents accidental hot-path SyscallLatency::load() calls
    // structurally.
    //
    //     auto h = crucible::perf::SyscallLatency::load(crucible::effects::Init{});
    //
    // Diagnostic line printed to stderr unless CRUCIBLE_PERF_QUIET=1
    // (or the legacy CRUCIBLE_BENCH_BPF_QUIET=1) is set.
    // CRUCIBLE_PERF_VERBOSE=1 forwards libbpf's INFO/WARN messages.
    [[nodiscard]] static std::optional<SyscallLatency>
        load(::crucible::effects::Init) noexcept;

    // Total syscalls recorded for our process since load().  Cost:
    // one bpf_map_lookup_elem syscall (~1 µs) — the total_syscalls
    // map is an ARRAY with ONE element, not mmap-able under the
    // current syscall_latency.bpf.c.  Returns 0 on a moved-from /
    // un-loaded SyscallLatency (no syscall issued in that case).
    //
    // Each event represents ONE return from any syscall — including
    // the bpf_map_lookup_elem this method itself issues, so calling
    // total_syscalls() in a tight loop measures the loop, not the
    // workload.  Use pre/post deltas across a workload region.
    [[nodiscard]] uint64_t total_syscalls() const noexcept;

    // Borrowed view over the syscall_timeline event ring buffer.
    // Spans exactly TIMELINE_CAPACITY events; the reader is
    // responsible for reading timeline_write_index() to find the
    // latest valid slot, then walking backwards while ts_ns != 0.
    //
    // The element type is `const TimelineSyscallEvent` (NOT `const
    // volatile`).  std::span<const volatile T> is currently not
    // supported by libstdc++ for non-scalar T.  The mmap memory IS
    // produced by the kernel concurrently with reader access, so
    // consumers should treat each event as racy: read ts_ns first
    // via `__atomic_load_n(&events[slot].ts_ns, __ATOMIC_ACQUIRE)`,
    // and only trust the other fields when ts_ns != 0 (the BPF
    // program writes ts_ns LAST as the completion marker).
    //
    // The returned span is empty (`view.empty() == true`) on a
    // moved-from / un-loaded SyscallLatency — use empty() to
    // discriminate.
    //
    // The Source phantom is SyscallLatency (the owning instance) —
    // the mmap lifetime is tied to the SyscallLatency dtor, so
    // consumers holding the Borrowed view past the SyscallLatency's
    // lifetime is a use-after-free.  CRUCIBLE_LIFETIMEBOUND on
    // Borrowed's ctor catches the simple temporary-bind cases at
    // compile time.
    [[nodiscard]] safety::Borrowed<const TimelineSyscallEvent, SyscallLatency>
        timeline_view() const noexcept;

    // Current value of the syscall_timeline ring buffer's write_idx
    // (monotonically increasing).  Reader uses this to identify the
    // most-recently-written slot: `(write_idx - 1) & TIMELINE_MASK`.
    // 0 on a moved-from / un-loaded SyscallLatency.
    //
    // Volatile load — no syscall, no atomic fence, just a single
    // u64 read from the shared mmap'd page.  ~1 ns.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // Number of bpf_link attachments the kernel accepted.  The
    // syscall_latency.bpf.c program contains exactly TWO
    // SEC("tracepoint/raw_syscalls/sys_{enter,exit}") programs —
    // both must attach for the facade to function (a half-attach
    // would record sys_enter timestamps that never get consumed on
    // exit, leaking syscall_start map entries until LRU eviction
    // kicks in).  load() returns nullopt if either program fails to
    // attach.  Cap of 8 matches the inplace_vector<...,8> shape
    // used by SchedSwitch / SenseHub / PmuSample / LockContention.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attached_programs() const noexcept;

    // Number of bpf_program__attach calls that failed (returned
    // NULL or an ERR_PTR).  Same bound as attached_programs().
    // Non-zero means at least one tracepoint was unavailable — set
    // CRUCIBLE_PERF_VERBOSE=1 to see which.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attach_failures() const noexcept;

    SyscallLatency(const SyscallLatency&) =
        delete("SyscallLatency owns unique BPF object + mmap — copying would double-close");
    SyscallLatency& operator=(const SyscallLatency&) =
        delete("SyscallLatency owns unique BPF object + mmap — copying would double-close");
    SyscallLatency(SyscallLatency&&) noexcept;
    SyscallLatency& operator=(SyscallLatency&&) noexcept;
    ~SyscallLatency();

 private:
    struct State;
    SyscallLatency() noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace crucible::perf
