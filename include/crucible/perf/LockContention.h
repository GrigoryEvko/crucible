#pragma once

// crucible::perf::LockContention — futex-wait drill-down via the
// syscalls/sys_enter_futex + sys_exit_futex tracepoints.
//
// Third per-program facade in the GAPS-004 series (after SenseHub,
// SchedSwitch, PmuSample).  Where SenseHub gives an aggregate
// `FUTEX_WAIT_OPS` counter and SchedSwitch attributes off-CPU time
// to context switches, LockContention attributes off-CPU time to
// the SPECIFIC FUTEX address that caused it.  Per-(futex_addr,
// stack_id) aggregation answers questions like:
//
//   "Mutex at 0x7f...a380 is contended 450 times this iteration,
//    avg wait 12 µs, hottest caller: image_filter::sharpen line 142"
//
// ─── Two costs, one facade ─────────────────────────────────────────
//
//   wait_count() —     single bpf_map_lookup_elem syscall, ~1 µs
//                      against the 1-element ARRAY map `lock_wait_count`.
//                      Total futex-wait events recorded for our
//                      process since load().  Cheap enough to call
//                      from a per-iteration loop in tests and benches;
//                      NOT for hot paths.
//
//   timeline_view() —  Borrowed span over the mmap'd `lock_timeline`
//                      ring buffer (4096 events × 32 B/event = 128 KB).
//                      Volatile reads of ts_ns identify completed
//                      events (the BPF program writes ts_ns LAST).
//                      Sub-µs per access.
//
// The hash-map drill-downs (`contention` keyed by (futex_addr,
// stack_id), `wait_start` keyed by tid) are NOT exposed in this
// first facade — they require hash-map iteration syscalls
// (BPF_MAP_GET_NEXT_KEY + BPF_MAP_LOOKUP_ELEM, ~1 µs per entry × N
// entries) which is a different cost class and a different API
// shape.  When GAPS-004x extracts the shared BpfLoader, the
// hash-map iteration helper will surface as a primitive in
// include/crucible/perf/detail/BpfLoader.h.
//
// ─── Production usage ──────────────────────────────────────────────
//
//     if (auto h = crucible::perf::LockContention::load(crucible::effects::Init{})) {
//         const uint64_t wait_pre = h->wait_count();
//         // ... run workload ...
//         const uint64_t wait_delta = h->wait_count() - wait_pre;
//
//         // Walk the timeline for the slowest waits:
//         const auto timeline = h->timeline_view();
//         const uint64_t write_idx = h->timeline_write_index();
//         // Reader logic per CLAUDE.md ring-buffer convention —
//         // walk write_idx backwards N events; trust each slot when
//         // ts_ns != 0, then sort by wait_ns to find the worst.
//     }
//
// If BPF is unavailable (no CAP_BPF / CAP_PERFMON, kernel too old,
// verifier rejects), load() returns nullopt and the caller proceeds
// with no per-futex drill-down.  All accessors on a moved-from /
// un-loaded LockContention return 0 / empty Borrowed / Refined{0}.
//
// ─── Known limits (inherited from lock_contention.bpf.c) ───────────
//
// (1) **Futex-wait only.**  This program traces sys_enter_futex /
//     sys_exit_futex with FUTEX_WAIT / FUTEX_WAIT_BITSET / FUTEX_LOCK_PI.
//     pthread_mutex (NPTL) goes through futex on contention, so
//     contended std::mutex / std::shared_mutex / pthread_mutex_t
//     waits are captured.  Uncontended locks (fast path = atomic
//     CAS, no syscall) are NOT — those are zero-cost and need no
//     contention attribution anyway.
//
// (2) **Per-tid `wait_start` keyed by raw tid.**  Multi-threaded
//     workloads can lose wait events when a thread is killed
//     mid-futex-wait (futex_enter recorded, no matching futex_exit
//     ever arrives).  GAPS-004d-AUDIT (2026-05-04) made this self-
//     healing by changing `wait_start` to `BPF_MAP_TYPE_LRU_HASH` —
//     orphaned entries auto-evict on capacity pressure rather than
//     accumulating to MAX_ENTRIES=65536 and silently blocking new
//     inserts via BPF_NOEXIST.  Same fix class as SchedSwitch
//     GAPS-004b-AUDIT for switch_start.  Coverage is unchanged
//     (we still record one entry per inflight futex_wait); the
//     leak is gone.
//
// (3) **Stack walk via BPF_F_USER_STACK + BPF_F_FAST_STACK_CMP** —
//     fast (no full DWARF unwind in BPF; uses frame-pointer walk
//     when available, falls back to LBR-assisted walk on Intel).
//     stack_id collisions (different stacks hashing to the same
//     bucket) are rare (~10⁻⁶ per stack walk) but CAN happen;
//     userspace symbol resolver MUST tolerate them.  Out of scope
//     for this facade — exposed via the hash-map iteration helper.
//
// (4) **Timeline ring-buffer slot reuse races on wrap.**  Same
//     limit as SchedSwitch — the 4096-event ring wraps every ~100ms
//     on a busy lock-contended workload.  Two cores both
//     incrementing write_idx get DIFFERENT idx values (good), but
//     `idx & MASK` can collide on the same slot after wrap if Core
//     A's BPF program is preempted between bump and write.  In
//     practice BPF runs with preemption disabled, so the window is
//     microseconds.  Documented limit; canonical fix is
//     BPF_MAP_TYPE_RINGBUF (kernel-side producer arbitration).
//     Out of scope for this facade.
//
// FIXED in GAPS-004d-AUDIT (2026-05-04):
// — `wait_start` map is now `BPF_MAP_TYPE_LRU_HASH` (was `HASH`);
//   orphaned entries auto-evict, no MAX_ENTRIES silent-rejection.
// — Compiler barrier (`__asm__ __volatile__ ("" ::: "memory")`)
//   inserted before the `ts_ns` store so clang -O2 cannot reorder
//   the field stores past the completion marker.  Pairs with the
//   userspace reader's `__atomic_load_n(&ts_ns, ACQUIRE)` —
//   "ts_ns LAST as completion marker" is now an enforced contract,
//   not wishful thinking.  Same fix class as SchedSwitch GAPS-004b-AUDIT.
// — Single `bpf_ktime_get_ns()` call per event (was two — one for
//   delta, one for ts_ns).  Matches SchedSwitch's canonical pattern;
//   saves one helper call (~50 ns per event).

#include <crucible/effects/Capabilities.h>  // effects::Init capability tag
#include <crucible/safety/Borrowed.h>       // safety::Borrowed<T, Source>
#include <crucible/safety/Refined.h>        // safety::Refined / bounded_above

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

// Mirrors `struct timeline_lock_event` in
// include/crucible/perf/bpf/common.h.  The BPF program writes
// fields in the order [futex_addr, wait_ns, tid, _pad, ts_ns] with
// ts_ns LAST; readers MUST check ts_ns != 0 before trusting the
// other fields.
//
// The struct layout is wire-load-bearing: the BPF program writes
// into the same byte offsets userspace reads.  static_assert below
// pins the 32-byte layout so an accidental member reorder or
// padding change fails compilation rather than producing garbage
// events.
//
// 32 bytes = futex_addr(8) + wait_ns(8) + tid(4) + _pad(4) + ts_ns(8).
// 32 divides 64 (cache line size) evenly, so each slot in the
// `events[TIMELINE_CAPACITY]` array sits cleanly within a single
// cache line — eliminating the torn-read window the SchedSwitch
// GAPS-004b-AUDIT documented (where the BPF program could observe
// ts_ns committed while other fields straddled a stale cache line).
struct TimelineLockEvent {
    uint64_t futex_addr;  //  8 B  futex address (userspace VA)
    uint64_t wait_ns;     //  8 B  duration in futex_wait (ns)
    uint32_t tid;         //  4 B  thread that returned from futex_wait
    uint32_t _pad;        //  4 B  pads ts_ns to 8-byte alignment
    uint64_t ts_ns;       //  8 B  bpf_ktime_get_ns() — WRITTEN LAST
};
static_assert(sizeof(TimelineLockEvent) == 32,
    "TimelineLockEvent must be 32 B (futex_addr 8 + wait_ns 8 + "
    "tid 4 + _pad 4 + ts_ns 8) to match the BPF program's "
    "struct timeline_lock_event in common.h — wire contract with "
    "BPF_F_MMAPABLE map.  32 divides 64 evenly, so events[N] never "
    "spans two cache lines.");

}  // namespace crucible::perf

// TimelineHeader + TIMELINE_CAPACITY + TIMELINE_MASK live in
// SchedSwitch.h (the first per-program facade to ship them).
// Including SchedSwitch.h here would create an unnecessary coupling
// — instead we re-declare them here against the same 64 B / 4096
// shape and static_assert that the layout matches.  The shared
// constants will move into a sibling include/crucible/perf/Timeline.h
// header during GAPS-004x (BpfLoader extraction).
#include <crucible/perf/SchedSwitch.h>  // for TimelineHeader, TIMELINE_CAPACITY

namespace crucible::perf {

class LockContention {
 public:
    // ─── Snapshot — consumer-shaped delta semantics ──────────────────
    //
    // Same shape as SchedSwitch::Snapshot: scalar (wait_count) plus
    // timeline write index.  `delta.wait_count` = futex_wait events
    // accumulated in the window; `delta.timeline_index` = events
    // produced into the ring (caps at TIMELINE_CAPACITY-overwrite).
    struct Snapshot {
        uint64_t wait_count     = 0;  // matches wait_count() at snapshot
        uint64_t timeline_index = 0;  // matches timeline_write_index() at snapshot

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            Snapshot r;
            if (__builtin_sub_overflow(wait_count, older.wait_count,
                                        &r.wait_count)) [[unlikely]] {
                r.wait_count = 0;
            }
            if (__builtin_sub_overflow(timeline_index, older.timeline_index,
                                        &r.timeline_index)) [[unlikely]] {
                r.timeline_index = 0;
            }
            return r;
        }
    };

    // Cost: dominated by wait_count() at ~1 µs (bpf_map_lookup_elem
    // syscall).  Same Keeper-tick-cadence guidance as SchedSwitch.
    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Load the embedded BPF program (lock_contention.bpf.c), set
    // target_tgid to getpid(), attach the syscalls/sys_enter_futex +
    // sys_exit_futex tracepoints, mmap the lock_timeline.  Returns
    // std::nullopt if any step fails (missing CAP_BPF / CAP_PERFMON,
    // kernel lacks the syscalls tracepoints, verifier rejects, etc.).
    //
    // The first parameter is a `::crucible::effects::Init`
    // capability tag (1 byte, EBO-collapsed at most call sites).
    // Same gate as SenseHub::load / SchedSwitch::load — hot-path and
    // background frames hold no Init token, so the cap-typing gate
    // prevents accidental hot-path LockContention::load() calls
    // structurally.
    //
    //     auto h = crucible::perf::LockContention::load(crucible::effects::Init{});
    //
    // Diagnostic line printed to stderr unless CRUCIBLE_PERF_QUIET=1
    // (or the legacy CRUCIBLE_BENCH_BPF_QUIET=1) is set in the
    // environment.  CRUCIBLE_PERF_VERBOSE=1 forwards libbpf's
    // INFO/WARN messages.
    [[nodiscard]] static std::optional<LockContention>
        load(::crucible::effects::Init) noexcept;

    // Total futex-wait events recorded for our process since load().
    // Cost: one bpf_map_lookup_elem syscall (~1 µs) — the
    // lock_wait_count map is an ARRAY with ONE element, not
    // mmap-able under the current lock_contention.bpf.c.  Returns 0
    // on a moved-from or un-loaded LockContention (no syscall
    // issued in that case).
    //
    // Each event represents ONE return from futex(FUTEX_WAIT* /
    // FUTEX_LOCK_PI) — uncontended locks (atomic CAS fast path) do
    // NOT count.  Pre/post deltas across a workload region give
    // "how many times did we block on contended locks".
    [[nodiscard]] uint64_t wait_count() const noexcept;

    // Borrowed view over the lock_timeline event ring buffer.
    // Spans exactly TIMELINE_CAPACITY events; the reader is
    // responsible for reading timeline_write_index() to find the
    // latest valid slot, then walking backwards while ts_ns != 0.
    //
    // The element type is `const TimelineLockEvent` (NOT `const
    // volatile`).  std::span<const volatile T> is currently not
    // supported by libstdc++ for non-scalar T (the std::span
    // implementation instantiates a copy ctor that fails under
    // volatile).  The mmap memory IS produced by the kernel
    // concurrently with reader access, so consumers should treat
    // each event as racy: read ts_ns first via a volatile / atomic
    // load (e.g. `__atomic_load_n(&events[slot].ts_ns,
    // __ATOMIC_ACQUIRE)`), and only trust the other fields when
    // ts_ns != 0 (the BPF program writes ts_ns LAST as the
    // completion marker).  See common.h's wire-contract comment
    // for the producer-side discipline.
    //
    // The returned span is empty (`view.empty() == true`) on a
    // moved-from / un-loaded LockContention — use empty() to
    // discriminate.
    //
    // The Source phantom is LockContention (the owning instance) —
    // the mmap lifetime is tied to the LockContention dtor, so
    // consumers holding the Borrowed view past the LockContention's
    // lifetime is a use-after-free.  CRUCIBLE_LIFETIMEBOUND on
    // Borrowed's ctor catches the simple temporary-bind cases at
    // compile time.
    [[nodiscard]] safety::Borrowed<const TimelineLockEvent, LockContention>
        timeline_view() const noexcept;

    // Current value of the lock_timeline ring buffer's write_idx
    // (monotonically increasing).  Reader uses this to identify the
    // most-recently-written slot: `(write_idx - 1) & TIMELINE_MASK`.
    // 0 on a moved-from / un-loaded LockContention.
    //
    // Volatile load — no syscall, no atomic fence, just a single u64
    // read from the shared mmap'd page.  ~1 ns.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // Number of bpf_link attachments the kernel accepted.  The
    // lock_contention.bpf.c program contains exactly TWO
    // SEC("tracepoint/syscalls/sys_{enter,exit}_futex") programs —
    // both must attach for the facade to function (a half-attach
    // would record futex_enter timestamps that never get consumed
    // on exit, leaking wait_start map entries until MAX_ENTRIES).
    // load() returns nullopt if either program fails to attach.
    // Cap of 8 matches the inplace_vector<...,8> shape used by
    // SchedSwitch / SenseHub.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attached_programs() const noexcept;

    // Number of bpf_program__attach calls that failed (returned
    // NULL or an ERR_PTR).  Same bound as attached_programs().
    // Non-zero means at least one tracepoint was unavailable — set
    // CRUCIBLE_PERF_VERBOSE=1 to see which.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attach_failures() const noexcept;

    LockContention(const LockContention&) =
        delete("LockContention owns unique BPF object + mmap — copying would double-close");
    LockContention& operator=(const LockContention&) =
        delete("LockContention owns unique BPF object + mmap — copying would double-close");
    LockContention(LockContention&&) noexcept;
    LockContention& operator=(LockContention&&) noexcept;
    ~LockContention();

 private:
    struct State;
    LockContention() noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace crucible::perf
