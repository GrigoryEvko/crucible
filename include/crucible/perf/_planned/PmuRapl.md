# `PmuRapl` — RAPL energy counters per power domain

**STATUS**: doc-only stub.  Tier-A.  Eventual path:
`include/crucible/perf/PmuRapl.h`.  Userspace-only.

## Problem

Bench reports cycles, instructions, ns.  It doesn't report **joules**.
Per-region energy is the missing axis for "is this code energy-
efficient" questions — increasingly important as Crucible deployments
include hyperscaler power-cap envelopes and edge-deployment battery
budgets.

## Mechanism

RAPL (Running Average Power Limit) — Intel since Sandy Bridge, AMD
since Zen — exposes per-power-domain energy via MSRs.  Linux exposes
these as a `power` PMU type:

```cpp
struct perf_event_attr attr{};
attr.type   = PERF_TYPE_POWER;            // dynamic; read from
                                          // /sys/bus/event_source/devices/power/type
attr.config = 0x1;  // PSys (Intel) — total platform energy
                    // 0x2 = PKG (CPU package), 0x3 = DRAM,
                    // 0x4 = PP0 (cores), 0x5 = PP1 (uncore/GPU)
```

Counters return cumulative energy in microjoules (Intel) or kilojoules
(scaled per `/sys/bus/event_source/devices/power/events/energy-pkg.scale`).

## API shape

```cpp
struct PmuRaplSnapshot {
    uint64_t pkg_uj;             // package
    uint64_t cores_uj;           // PP0
    uint64_t uncore_or_gpu_uj;   // PP1 (Intel) / GFX (AMD)
    uint64_t dram_uj;
    uint64_t psys_uj;            // platform (Intel server only)
    [[nodiscard]] PmuRaplSnapshot operator-(const PmuRaplSnapshot&) const noexcept;
};

class PmuRapl {
public:
    [[nodiscard]] static std::optional<PmuRapl> load(::crucible::effects::Init) noexcept;
    [[nodiscard]] PmuRaplSnapshot read() const noexcept;
};
```

## Cost

- `read()`: ~50-100 ns (5 MSR reads through perf_event_open path; not
  RDPMC-eligible — kernel mediates).
- Per-iteration: 0 ns.

## Known limits

- Per-package; can't attribute energy to a specific core/process
  (RAPL is socket-wide).  For per-process energy, multiply by
  CPU-time fraction (TASK_CLOCK / total wall) — approximation.
- RAPL counters wrap (~32-bit at high power, ~minutes for total
  energy).  Userspace must check delta < prev and treat as wrap.
- DRAM domain on Intel client / AMD desktop is often unsupported
  (`access` returns -1; load() reports partial coverage).
- Intel server RAPL has been historically inaccurate on idle (over-
  reports by ~5-10%); used as relative comparison, not absolute
  metering.

## Integration

Bench harness adds: `└─ ... energy=42.3 mJ (pkg 38.1 / cores 22.0 / dram 4.2)`

For long-running production workloads, sample once per minute and
log; multiply by deployment-known $/MWh for cost attribution.

## Sibling refs

- `PmuCounters` (same perf_event_open shape)
- `cpu_idle.bpf.c` (idle state residency — paired with RAPL gives
  "energy / non-idle time" = non-idle power draw)
