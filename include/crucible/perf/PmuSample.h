#pragma once

// crucible::perf::PmuSample — Hardware PMU sampling via BPF.
//
// Second per-program facade in the GAPS-004 series (sibling to
// SchedSwitch).  Where SenseHub aggregates and SchedSwitch
// attributes off-CPU time, PmuSample answers "WHERE in the code
// is the cache/branch/TLB cost happening" via PMU counter
// overflow IPs (instruction pointers).
//
// ─── What it captures ───────────────────────────────────────────────
//
// 8 event types, all flowing into one shared 32K-event mmap'd ring:
//
//   2: LlcMiss          — last-level cache miss IP
//   3: BranchMiss       — branch misprediction IP
//   4: DtlbMiss         — data TLB miss IP
//   5: IbsOp            — AMD IBS-Op (PRECISE micro-op IP, zero skid)
//   6: IbsFetch         — AMD IBS-Fetch (instruction fetch IP)
//   7: MajorPageFault   — IP of the faulting load/store
//   8: CpuMigration     — IP at the moment of migration
//   9: AlignmentFault   — IP of the misaligned access
//
// IBS event types (5, 6) auto-detect AMD support at load time;
// non-AMD systems skip those attachments without failing the
// load — `attach_failures()` reports the dropped count.
//
// ─── Cost model — OPT-IN profiling ─────────────────────────────────
//
// Unlike SenseHub (always-on aggregation) and SchedSwitch (cheap
// off-CPU drill-down), PmuSample is INTRINSICALLY a profiling
// tool — every overflow event triggers our BPF program in
// NMI/PMI context.  Per-event BPF cost: ~3-5 µs.
//
// At default sample periods:
//   HW events (LLC/Branch/DTLB): period 10000 → ~10-100 events/sec
//                                each on typical workloads
//   IBS:                         period 100000 → ~100-1000/sec
//   SW events (page faults etc): period 1 → every event, but
//                                native rate is 10-100/sec
//
// Total event rate: ~500-2000/sec → ~2.5-10 ms/sec BPF overhead
// = 0.25-1% CPU.  Bench harnesses that load PmuSample explicitly
// pay this cost; one-shot diagnosis runs find it negligible.
//
// ─── Production usage ──────────────────────────────────────────────
//
//     auto h = crucible::perf::PmuSample::load(crucible::effects::Init{});
//     if (h) {
//         const uint64_t pre  = h->timeline_write_index();
//         // ... run workload ...
//         const uint64_t post = h->timeline_write_index();
//         const auto view = h->timeline_view();
//         for (uint64_t i = pre; i < post; ++i) {
//             const uint32_t slot = i & PMU_SAMPLE_MASK;
//             const uint64_t ts = __atomic_load_n(&view.data()[slot].ts_ns,
//                                                 __ATOMIC_ACQUIRE);
//             if (ts == 0) continue;
//             // ... aggregate by view.data()[slot].ip + event_type ...
//         }
//     }
//
// If perf_event_open is unavailable (paranoid >2 without
// CAP_PERFMON, kernel too old), load() returns nullopt.
//
// ─── Sample-period env-var knobs (GAPS-004c-AUDIT, #1290) ──────────
//
// At load() time, the per-event-type sample periods can be
// overridden via env vars (read once with the same caching
// discipline as CRUCIBLE_PERF_QUIET — setenv AFTER load() has no
// effect):
//
//   CRUCIBLE_PERF_PMU_PERIOD_HW   — overrides LLC/Branch/DTLB
//                                   (default: 10000)
//   CRUCIBLE_PERF_PMU_PERIOD_IBS  — overrides IBS-Op / IBS-Fetch
//                                   (default: 100000)
//   CRUCIBLE_PERF_PMU_PERIOD_SW   — overrides MajorPF / CpuMig /
//                                   AlignFault (default: 1)
//
// Lower → denser sampling → higher BPF cost.  Higher → sparser
// → lower cost but more aliasing in hot-loop attribution.
// Recommended ranges:
//   HW : 1000 (very dense, ~10% CPU on cache-bound) to
//        100000 (sparse, ~0.1% CPU)
//   IBS: 10000 (precise, dense) to 1000000 (low overhead)
//   SW : 1 (every event) to 100 (subsample)
//
// CRUCIBLE_PERF_VERBOSE=1 prints which event types attached and
// at what period — useful for verifying the override took effect.
//
// ─── Reader-side guidance ──────────────────────────────────────────
//
// When walking timeline_view():
//   • Always do `__atomic_load_n(&events[slot].ts_ns, ACQUIRE)`
//     FIRST.  ts_ns == 0 → producer hasn't committed this slot
//     yet (or never wrote it).  Skip.
//   • ts_ns != 0 → other fields are safe to read (the BPF
//     program's compiler barrier ensures ip/tid/event_type
//     stores retired before ts_ns).
//   • ip == 0 is unusual but possible — typically means the
//     instruction at the sample point was at virtual address 0
//     (extremely rare; usually a corrupt context).  Skip such
//     samples in your aggregation rather than counting them as
//     a real instruction.
//
// ─── Known limits (GAPS-004c-AUDIT, 2026-05-04) ────────────────────
//
// (1) **Main-thread-only coverage.**  perf_event_open(pid=tgid,
//     cpu=-1) monitors the kernel task whose TID == TGID, which is
//     the main thread of the process.  Other threads in the same
//     process are NOT sampled.  Same multi-thread coverage gap as
//     SchedSwitch's our_tids registration.  Workaround for now:
//     run profiling-targeted code on the main thread, or accept
//     partial coverage.  Long-term fix (GAPS-004x): enumerate
//     /proc/self/task/* and open one perf_event per TID, with
//     a sched_process_exit hook to clean up dead threads (avoids
//     TID-reuse hazard).
//
// (2) **Ring-buffer slot reuse on wrap.**  Same architectural
//     limit as SchedSwitch's sched_timeline.  32K events × 24 µs
//     between samples (typical busy workload) ≈ 768 ms wrap
//     window — much wider than SchedSwitch's 100 ms but still
//     bounded.  BPF preempt-disabled execution narrows the
//     practical race window to microseconds.  Canonical fix is
//     BPF_MAP_TYPE_RINGBUF; out of scope for this facade.

#include <crucible/effects/Capabilities.h>  // effects::Init
#include <crucible/safety/Borrowed.h>       // safety::Borrowed
#include <crucible/safety/Refined.h>        // safety::Refined / bounded_above

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

// Discriminator values for `event_type`.  The numeric values are
// wire-protocol with the kernel-side BPF program at
// include/crucible/perf/bpf/pmu_sample.bpf.c — must NOT renumber.
enum class PmuEventType : uint8_t {
    // 0, 1 reserved (cycles, L1D miss flow through perf_event ring,
    // not BPF — they're too frequent).
    LlcMiss          = 2,
    BranchMiss       = 3,
    DtlbMiss         = 4,
    IbsOp            = 5,   // AMD precise (zero skid), non-AMD: skipped
    IbsFetch         = 6,   // AMD precise instruction fetch
    MajorPageFault   = 7,
    CpuMigration     = 8,
    AlignmentFault   = 9,
};

// Mirrors `struct pmu_sample_event` in include/crucible/perf/bpf/common.h.
// 32 bytes (24 B payload + 8 B trailing pad) for cache-line
// coresidence — see GAPS-004c (2026-05-04) — without the pad,
// 32-byte slots in the ring would straddle 64 B lines and torn
// reads would silently violate the "ts_ns LAST as completion
// marker" contract for the slots crossing a boundary.
struct PmuSampleEvent {
    uint64_t ip;          //  8 B  userspace virtual address
    uint32_t tid;         //  4 B  thread ID
    uint8_t  event_type;  //  1 B  PmuEventType discriminator
    uint8_t  _pad[3];     //  3 B  align ts_ns to 8 bytes
    uint64_t ts_ns;       //  8 B  bpf_ktime_get_ns() — WRITTEN LAST
    uint64_t _pad8;       //  8 B  cache-line-coresidence pad
};
static_assert(sizeof(PmuSampleEvent) == 32,
    "PmuSampleEvent must be 32 B = ip(8) + tid(4) + event_type(1) + "
    "_pad[3] + ts_ns(8) + _pad8(8); the trailing pad makes 32 divide "
    "64 evenly so each slot is cache-line-coresident");

// Same TimelineHeader shape as SchedSwitch.  Defined separately
// here to keep the headers independent (GAPS-004x extraction will
// unify).
struct PmuSampleHeader {
    uint64_t write_idx;   //  8 B  monotonically increasing
    uint64_t _pad[7];     // 56 B  pad to one cache line
};
static_assert(sizeof(PmuSampleHeader) == 64,
    "PmuSampleHeader must be exactly one cache line so the events "
    "array starts at offset 64");

// 32K events × 32 B = 1 MB ring buffer.  Bigger than SchedSwitch's
// 4K (96 KB) because PMU sampling rates can briefly burst much
// higher than sched_switch rates, especially under cache-thrashing
// workloads where a single hot loop can fire 10K+ LLC misses/sec.
constexpr uint32_t PMU_SAMPLE_CAPACITY = 32768;
constexpr uint32_t PMU_SAMPLE_MASK     = PMU_SAMPLE_CAPACITY - 1;

class PmuSample {
 public:
    // Load embedded BPF program, set target_tgid to getpid(),
    // perf_event_open all 8 event types tracking this PID across
    // all CPUs (cpu=-1), attach BPF programs, mmap the
    // pmu_sample_buf.  Returns nullopt if any of:
    //   - missing CAP_PERFMON (and paranoid > 2)
    //   - perf_event_open returns -1 for ALL event types
    //   - bpf_object__load fails (verifier reject)
    //
    // Partial success is OK: if 6 of 8 event types attach
    // successfully (e.g. non-AMD system: IBS skipped), load()
    // returns the populated PmuSample and `attach_failures()`
    // reports the count.
    //
    // First parameter is `effects::Init` capability tag — same
    // hot-path-cannot-construct-it gate as SenseHub/SchedSwitch.
    [[nodiscard]] static std::optional<PmuSample>
        load(::crucible::effects::Init) noexcept;

    // Borrowed view over the timeline event ring buffer.  Spans
    // exactly PMU_SAMPLE_CAPACITY events; reader uses
    // timeline_write_index() to find the latest valid slot.  The
    // returned span is empty (`empty() == true`) on a moved-from
    // / un-loaded PmuSample.  Element type is `const PmuSampleEvent`
    // (not `const volatile`) for the same libstdc++ <span>
    // composability reason as SchedSwitch — readers must do
    // `__atomic_load_n(&events[slot].ts_ns, __ATOMIC_ACQUIRE)`
    // first; ts_ns != 0 means the producer-side compiler-barrier
    // store has retired and other fields are safe.
    [[nodiscard]] safety::Borrowed<const PmuSampleEvent, PmuSample>
        timeline_view() const noexcept;

    // Volatile load of the ring header's write_idx (monotonically
    // increasing).  ~1 ns; no syscall.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // Number of perf_event programs the kernel accepted.  Bounded
    // above by 8 (one per PmuEventType) — non-AMD systems get 6
    // (IBS-Op / IBS-Fetch skipped); systems with paranoid >= 1
    // for HW events get 3 (just SW); etc.  `(post)/8` is the
    // structural rate.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attached_programs() const noexcept;

    // Number of perf_event_open + bpf_program__attach calls that
    // failed.  `attached_programs() + attach_failures()` ≤ 8.
    // Set CRUCIBLE_PERF_VERBOSE=1 to see why each one failed.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attach_failures() const noexcept;

    PmuSample(const PmuSample&) =
        delete("PmuSample owns unique BPF object + perf_event FDs + mmap");
    PmuSample& operator=(const PmuSample&) =
        delete("PmuSample owns unique BPF object + perf_event FDs + mmap");
    PmuSample(PmuSample&&) noexcept;
    PmuSample& operator=(PmuSample&&) noexcept;
    ~PmuSample();

 private:
    struct State;
    PmuSample() noexcept;
    std::unique_ptr<State> state_;
};

}  // namespace crucible::perf
