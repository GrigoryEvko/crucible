#pragma once

// crucible::perf::SchedSwitch — Off-CPU drill-down via the
// sched_switch tracepoint.
//
// First per-program facade in the GAPS-004 series (after the
// keystone SenseHub aggregator).  Where SenseHub answers "is
// something slow" with cheap, always-on global counters, SchedSwitch
// answers "where is the off-CPU time going" — per-stack-id /
// per-tid attribution of context-switch durations, plus a zero-copy
// mmap'd timeline ring buffer of recent off-CPU events.
//
// ─── Two costs, one facade ─────────────────────────────────────────
//
//   context_switches() — single bpf_map_lookup_elem syscall, ~1 µs.
//                        Cheap enough to call from a per-iteration
//                        loop in tests and benchmarks; NOT for hot
//                        paths.  The cs_count BPF map is a 1-element
//                        ARRAY map (NOT mmap-able as currently
//                        declared in sched_switch.bpf.c — upgrading
//                        to BPF_F_MMAPABLE would let us eliminate
//                        the syscall, but is a kernel-side BPF
//                        program change that lands separately).
//
//   timeline_view() —    Borrowed span over the mmap'd
//                        sched_timeline ring buffer (4096 events ×
//                        24 B/event = 96 KB).  Volatile reads of
//                        ts_ns identify completed events
//                        (the BPF program writes ts_ns LAST).
//                        Sub-µs per access.
//
// The hash-map drill-downs (offcpu / switch_start / switch_stack /
// our_tids) are NOT exposed in this first facade — they require
// hash-map iteration syscalls (BPF_MAP_GET_NEXT_KEY +
// BPF_MAP_LOOKUP_ELEM, ~1 µs per entry × N entries) which is a
// different cost class and a different API shape.  When the second
// per-program facade lands (GAPS-004c PmuSample, GAPS-004d
// LockContention) and the loader pattern proves itself across two
// instances, the hash-map iteration helper will surface as a
// shared primitive in include/crucible/perf/detail/BpfLoader.h
// (GAPS-004x).
//
// ─── Production usage ──────────────────────────────────────────────
//
//     if (auto h = crucible::perf::SchedSwitch::load(crucible::effects::Init{})) {
//         const uint64_t cs_pre = h->context_switches();
//         // ... run workload ...
//         const uint64_t cs_post = h->context_switches();
//         const uint64_t cs_delta = cs_post - cs_pre;
//
//         // Walk the timeline for off-CPU spikes:
//         const auto timeline = h->timeline_view();
//         // Reader logic per CLAUDE.md ring-buffer convention —
//         // walk write_idx() backwards N events; trust each slot
//         // when ts_ns != 0.
//     }
//
// If BPF is unavailable (no CAP_BPF / CAP_PERFMON, kernel too old,
// verifier rejects), load() returns nullopt and the caller proceeds
// with no per-stack drill-down.  context_switches() on a moved-from
// or empty SchedSwitch returns 0.
//
// ─── Known limits (GAPS-004b-AUDIT, 2026-05-04) ────────────────────
//
// These are documented gaps to be addressed in GAPS-004x (BpfLoader
// extraction) or in a kernel-side BPF program update.  They do NOT
// affect correctness for the keystone use case (single-threaded
// off-CPU profiling of the loader's own thread); they bound the
// observability accuracy in multi-threaded / high-rate scenarios.
//
// (1) **our_tids holds only the loader's main TID.**  Multi-threaded
//     workloads will MISS off-CPU events for non-main threads — the
//     BPF program populates switch_start[other_tid] on switch-OUT,
//     but never finds other_tid in our_tids on switch-IN, so the
//     entry never gets popped.  After ~65536 such orphaned entries,
//     the switch_start hash map's LRU eviction silently drops the
//     oldest pending switch-OUT records.  Workaround for now: only
//     load() from a single-threaded process or accept partial
//     coverage.  GAPS-004x will iterate /proc/self/task/* and
//     populate every TID at load time.
//
// (2) **switch_stack[] map leaks on thread exit.**  When a thread
//     switches OUT (populates switch_stack[tid]) and never switches
//     back IN — e.g. it exits — switch_stack[tid] is never deleted.
//     Same MAX_ENTRIES=65536 LRU pressure as (1).  Workaround: long-
//     running profiles that span many thread births/deaths should
//     load() periodically rather than holding the SchedSwitch for
//     the entire run.  Long-term fix: BPF-side sched_process_exit
//     tracepoint paired delete.
//
// (3) **Timeline events that span two cache lines are not torn-write
//     atomic.**  Each TimelineSchedEvent is 24 bytes; events at
//     slots whose byte offset crosses a 64-byte boundary (most
//     slots, since 24 doesn't divide 64) span TWO cache lines.  The
//     BPF program's `events[slot].field = ...` writes are u64-
//     atomic at the field level (each member is naturally aligned),
//     but a reader observing one cache line in MODIFIED state and
//     the other in SHARED state may see some fields updated and
//     some stale.  The ts_ns-LAST completion-marker discipline
//     mitigates: the reader checks ts_ns != 0 first; only the
//     final cache line's atomic store reveals ts_ns, which for
//     8-byte-aligned ts_ns_offset==16 always lives in the SAME
//     cache line as on_cpu (offset 12, also in the second half).
//     Off_cpu_ns (offset 0) and tid (offset 8) live in the first
//     half.  So torn reads CAN occur where ts_ns and on_cpu are
//     visible but off_cpu_ns and tid are stale.  Workaround:
//     consumers that need exact off_cpu_ns/tid pairing should
//     compare ts_ns BEFORE and AFTER reading the other fields and
//     retry on mismatch (typical seqlock pattern, not yet wrapped
//     in a helper).  The BPF program's claim in common.h about
//     "all fields share the same cache line" is INCORRECT for
//     non-cache-line-aligned slots; documented here so the next
//     consumer doesn't re-discover it.
//
// (4) **Timeline ring-buffer slot reuse races on wrap.**  The 4096-
//     event ring wraps every ~100ms on a busy system (one
//     sched_switch every ~25 µs).  Two cores both incrementing
//     write_idx get DIFFERENT idx values (good), but `idx & MASK`
//     can collide on the same slot after wrap, leading to two
//     producers concurrently writing the SAME slot.  This is a
//     known limit of the BPF_MAP_TYPE_ARRAY mmap'd ring approach;
//     the canonical fix is BPF_MAP_TYPE_RINGBUF (kernel-side ring
//     with built-in producer arbitration).  Out of scope for the
//     C++ facade; tracked for the eventual BPF program upgrade.

#include <crucible/effects/Capabilities.h>  // effects::Init capability tag
#include <crucible/safety/Borrowed.h>       // safety::Borrowed<T, Source>
#include <crucible/safety/Refined.h>        // safety::Refined / bounded_above

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

// Mirrors `struct timeline_sched_event` in
// include/crucible/perf/bpf/common.h.  The BPF program writes
// fields in the order [off_cpu_ns, tid, on_cpu, ts_ns] with ts_ns
// LAST; readers MUST check ts_ns != 0 before trusting the other
// fields (incomplete writes are visible until ts_ns lands).
//
// The struct layout is wire-load-bearing: the BPF program writes
// into the same byte offsets userspace reads.  static_assert below
// pins the tight 24-byte layout so an accidental member reorder or
// padding insertion fails compilation rather than producing
// garbage events.
struct TimelineSchedEvent {
    uint64_t off_cpu_ns;  //  8 B  duration thread was off-CPU (ns)
    uint32_t tid;         //  4 B  thread that switched IN
    uint32_t on_cpu;      //  4 B  CPU core thread switched onto
    uint64_t ts_ns;       //  8 B  bpf_ktime_get_ns() — WRITTEN LAST
};
static_assert(sizeof(TimelineSchedEvent) == 24,
    "TimelineSchedEvent must be tight 24 B to match the BPF "
    "program's struct timeline_sched_event in common.h — wire "
    "contract with BPF_F_MMAPABLE map");

// Mirrors `struct timeline_header` in common.h.  64 B (one cache
// line) so the events array starts cache-aligned.  write_idx is
// monotonically incremented by the BPF program via
// __sync_fetch_and_add — readers compute the latest valid slot as
// `(write_idx - 1) & TIMELINE_MASK`.
struct TimelineHeader {
    uint64_t write_idx;   //  8 B  monotonically increasing
    uint64_t _pad[7];     // 56 B  pad to 64 B (one cache line)
};
static_assert(sizeof(TimelineHeader) == 64,
    "TimelineHeader must be exactly one cache line so the events "
    "array starts at offset 64 — pinned by BPF program layout");

// Number of events in the sched_timeline ring buffer.  Power of
// two so the modulo via mask is one bitwise AND.  Mirror of
// TIMELINE_CAPACITY in common.h — bumping this constant on either
// side without the other re-build breaks the wire contract.
constexpr uint32_t TIMELINE_CAPACITY = 4096;
constexpr uint32_t TIMELINE_MASK     = TIMELINE_CAPACITY - 1;

class SchedSwitch {
 public:
    // Load the embedded BPF program (sched_switch.bpf.c), set
    // target_tgid to getpid(), populate our_tids with our main
    // TID, attach the sched_switch tracepoint, mmap the
    // sched_timeline.  Returns std::nullopt if any step fails
    // (missing CAP_BPF / CAP_PERFMON, kernel lacks the tracepoint,
    // verifier rejects, etc.).
    //
    // The first parameter is a `::crucible::effects::Init`
    // capability tag (1 byte, EBO-collapsed at most call sites).
    // Same gate as SenseHub::load — hot-path and background frames
    // hold no Init token, so the cap-typing gate prevents
    // accidental hot-path SchedSwitch::load() calls structurally.
    //
    //     auto h = crucible::perf::SchedSwitch::load(crucible::effects::Init{});
    //
    // Diagnostic line printed to stderr unless CRUCIBLE_PERF_QUIET=1
    // (or the legacy CRUCIBLE_BENCH_BPF_QUIET=1) is set in the
    // environment.  CRUCIBLE_PERF_VERBOSE=1 forwards libbpf's
    // INFO/WARN messages.
    [[nodiscard]] static std::optional<SchedSwitch>
        load(::crucible::effects::Init) noexcept;

    // Total context switches recorded for our process since load().
    // Cost: one bpf_map_lookup_elem syscall (~1 µs) — the cs_count
    // BPF map is an ARRAY with ONE element, not mmap-able under the
    // current sched_switch.bpf.c.  Returns 0 on a moved-from or
    // un-loaded SchedSwitch (no syscall issued in that case).
    [[nodiscard]] uint64_t context_switches() const noexcept;

    // Borrowed view over the timeline event ring buffer.  Spans
    // exactly TIMELINE_CAPACITY events; the reader is responsible
    // for reading timeline_write_index() to find the latest valid
    // slot, then walking backwards while ts_ns != 0.
    //
    // The element type is `const TimelineSchedEvent` (NOT `const
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
    // moved-from / un-loaded SchedSwitch — use empty() to discriminate.
    //
    // The Source phantom is SchedSwitch (the owning instance) — the
    // mmap lifetime is tied to the SchedSwitch dtor, so consumers
    // holding the Borrowed view past the SchedSwitch's lifetime is
    // a use-after-free.  CRUCIBLE_LIFETIMEBOUND on Borrowed's ctor
    // catches the simple temporary-bind cases at compile time.
    [[nodiscard]] safety::Borrowed<const TimelineSchedEvent, SchedSwitch>
        timeline_view() const noexcept;

    // Current value of the sched_timeline ring buffer's write_idx
    // (monotonically increasing).  Reader uses this to identify the
    // most-recently-written slot: `(write_idx - 1) & TIMELINE_MASK`.
    // 0 on a moved-from / un-loaded SchedSwitch.
    //
    // Volatile load — no syscall, no atomic fence, just a single u64
    // read from the shared mmap'd page.  ~1 ns.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // Number of bpf_link attachments the kernel accepted.  The
    // sched_switch.bpf.c program contains exactly one
    // SEC("tracepoint/...") program, so this is 0 or 1 in practice
    // — but the cap is set at 8 to share the inplace_vector<...,8>
    // shape with future per-program facades that may attach more
    // probes (e.g. a future sched_wakeup pairing).  Same
    // bounded_above<8> envelope as attach_failures().
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attached_programs() const noexcept;

    // Number of bpf_program__attach calls that failed (returned
    // NULL or an ERR_PTR).  Same bound as attached_programs().
    // Non-zero means the tracepoint was unavailable — set
    // CRUCIBLE_PERF_VERBOSE=1 to see why.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attach_failures() const noexcept;

    SchedSwitch(const SchedSwitch&) =
        delete("SchedSwitch owns unique BPF object + mmap — copying would double-close");
    SchedSwitch& operator=(const SchedSwitch&) =
        delete("SchedSwitch owns unique BPF object + mmap — copying would double-close");
    SchedSwitch(SchedSwitch&&) noexcept;
    SchedSwitch& operator=(SchedSwitch&&) noexcept;
    ~SchedSwitch();

 private:
    struct State;
    SchedSwitch() noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace crucible::perf
