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
// PERF_TYPE_POWER does NOT exist as a UAPI constant.  RAPL uses a
// DYNAMIC type discovered at runtime:
int rapl_type = read_dynamic_pmu_type(
    "/sys/bus/event_source/devices/power/type");
struct perf_event_attr attr{};
attr.type   = rapl_type;
attr.config = 0x1;  // rapl_energy_cores (PP0)
                    // 0x2 = rapl_energy_pkg (CPU package)
                    // 0x3 = rapl_energy_ram (DRAM, server only)
                    // 0x4 = rapl_energy_gpu (PP1, client only)
                    // 0x5 = rapl_energy_psys (Intel platform)
```

Codes verified against arch/x86/events/rapl.c on 6.17 — the canonical
config codes are pp0=0x1, pkg=0x2, dram=0x3.  /sys/bus/event_source/
devices/power/events/* lists which codes the local CPU exposes.

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
