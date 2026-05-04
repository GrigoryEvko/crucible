# `PmuTopDown` — Intel/AMD TopDown 4-quadrant pipeline classification

**STATUS**: doc-only stub.  Tier-A.  Eventual path:
`include/crucible/perf/PmuTopDown.h`.  Userspace-only.

## Problem

Intel TopDown methodology classifies every cycle into ONE of four
quadrants:
- **Frontend Bound** — pipeline stalled fetching/decoding instructions
- **Bad Speculation** — branch mispredict / TSX abort wasted cycles
- **Backend Bound** — pipeline stalled waiting for data / ALU
- **Retiring** — productive cycles

The 4 quadrant percentages tell you the SHAPE of the workload's
microarch behavior in one glance.  Vastly more actionable than
"cache misses are high" because you immediately know "is the bottleneck
in the front of the pipeline (icache, BTB) or the back (memory, ALU)".

## Mechanism

Intel Icelake+ (and later — Sapphire Rapids, Granite Rapids) expose
TopDown values directly via `MSR_PERF_METRICS` (0x329) + fixed counter
3 (slots).  Verified against arch/x86/events/intel/core.c on 6.17:
TOPDOWN_BAD_SPECULATION.ALL (0x0073), TOPDOWN_FE_BOUND.ALL (0x019c),
TOPDOWN_RETIRING.ALL (0x02c2), TOPDOWN_BE_BOUND.ALL via the metric.

AMD does NOT ship MSR_PERF_METRICS (no references in
arch/x86/events/amd/ as of 6.17 mainline).  AMD Zen TopDown-equivalent
analysis must derive from raw IBS_OP / pipeline-utilization events;
materially different mechanism — file a sibling PmuAmdPipeUtil.h when
needed rather than pretending one PMU surface fits both.

```cpp
// Intel SPR+ path:
struct perf_event_attr attr{};
attr.type   = PERF_TYPE_RAW;
attr.config = 0x400;  // PERF_METRICS_TOPDOWN_RETIRING
// Plus 3 more for FRONTEND_BOUND / BAD_SPECULATION / BACKEND_BOUND.
attr.sample_period = 0;
attr.read_format   = PERF_FORMAT_GROUP;
```

Open as a 4-event group with `MSR_PERF_METRICS`-backed config; one
group_fd RDPMC call returns all 4 values.

## API shape

```cpp
struct PmuTopDownSnapshot {
    uint64_t total_slots;       // pipeline-slots total
    uint64_t retiring_slots;
    uint64_t frontend_bound_slots;
    uint64_t bad_spec_slots;
    uint64_t backend_bound_slots;

    [[nodiscard]] PmuTopDownSnapshot operator-(const PmuTopDownSnapshot&) const noexcept;
    [[nodiscard]] double retiring_pct() const noexcept;
    [[nodiscard]] double frontend_bound_pct() const noexcept;
    [[nodiscard]] double bad_spec_pct() const noexcept;
    [[nodiscard]] double backend_bound_pct() const noexcept;
};

class PmuTopDown {
public:
    [[nodiscard]] static std::optional<PmuTopDown>
        load(::crucible::effects::Init) noexcept;
    [[nodiscard]] PmuTopDownSnapshot read() const noexcept;
    // ...
};
```

## Cost

- `read()`: ~10 ns (one group RDPMC across 4 quadrants).
- Per-iteration on bench: 0 ns.

## Limits

- Intel Icelake+ only (verified — kernel TopDown machinery in
  arch/x86/events/intel/core.c gates on PERF_X86_EVENT_TOPDOWN).
  AMD: NOT supported via this facade.  Older Intel + AMD return
  nullopt at load().
- ARM Neoverse has its own TopDown method (TopDown L1/L2) handled in
  drivers/perf/arm_pmu*.c — different config; if/when needed file
  PmuArmTopDown.h as a sibling.
- The 4 quadrants don't include "instructions retired vs total slots"
  resolution — for issue-port pressure breakdown, drill into
  TopDown L2 (8 sub-quadrants).  Out of scope for v1.

## Sibling refs

- `PmuCounters` — same `perf_event_open` infrastructure
- Augur's per-region "is this compute-bound or memory-bound?" question
  answers from this single facade
