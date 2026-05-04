# `DamonReader` — kernel DAMON memory-access pattern reader

**STATUS**: doc-only stub.  Tier-1 self-observation sibling.  Eventual
path: `include/crucible/perf/DamonReader.h`.  Userspace-only — DAMON
is a kernel subsystem (5.15+) configured via sysfs and observed via
tracepoints.

## Problem

`PmuSample` with DTLB-miss IP sampling tells us "this IP missed TLB
N times" — high signal but high cost (PEBS samples at ~1 kHz/CPU,
each with extended payload).  For coarse "what memory regions are
hot" attribution, **DAMON is much cheaper**: kernel-side scan of
PG_referenced bits across virtual address regions, no per-access
overhead, microsecond-resolution per-region access frequency.

DAMON answers questions PmuSample can't:
- "Which 4 MB region of our 80 GB heap saw the most accesses last 100 ms?"
- "Did our access pattern shift after the optimizer change?"
- "Should this region be promoted to NUMA-local / huge pages / migrated to fast tier?"

It's the cold-path complement to PmuSample's hot-path IP sampling.

## Mechanism

DAMON (Data Access MONitor, kernel 5.15+) operates kernel-side:

1. **Define monitoring scheme** — virtual address regions to observe per target process.
2. **Kernel scans** PG_referenced bits across regions at configurable interval (default 5 ms aggregation, 100 ms sample period).
3. **Region splitting/merging** based on access uniformity (auto-tune resolution).
4. **Counters exposed** as `nr_accesses` per region per aggregation window.

Userspace control via sysfs:

```
/sys/kernel/mm/damon/admin/kdamonds/<id>/contexts/0/
    monitoring_attrs/intervals/{sample_us,aggr_us,update_us}
    targets/0/{pid,regions/...}
    schemes/0/...
```

Observation paths:

### Path A: tracepoint (kernel ≥ 6.6, verified on 6.17)

```
/sys/kernel/tracing/events/damon/
    damon_aggregated         — fires every aggr_us with per-region counts
    damon_monitor_intervals_tune
    damos_before_apply       — scheme about to apply
    damos_esz                — extended size info
```

BPF-attachable, low rate (1 event per region per aggregation window).

### Path B: sysfs polling (kernel ≥ 5.15)

```
/sys/kernel/mm/damon/admin/kdamonds/<id>/contexts/0/monitoring_attrs/...
/sys/kernel/mm/damon/admin/.../schemes/0/stats/{nr_tried,nr_applied,...}
```

Userspace polls every aggr_us.  Slightly less direct but no BPF
required.

This facade uses Path A when kernel ≥ 6.6 (tracepoint exists), Path B
otherwise.

## Reader cadence

Default aggr_us = 100 ms → 10 events/sec/region.  For 100 regions:
1000 events/sec total.  Per-event cost ~50 ns BPF + ~200 ns userspace
decode = 0.025% CPU at 100 regions.  Effectively free.

## Configuration

Crucible startup creates one DAMON context targeting the Crucible
process, with regions auto-discovered (DAMON's `regions` start auto-
populated by walking `/proc/self/maps`).

```cpp
struct DamonConfig {
    uint64_t sample_us = 5000;        // 5 ms PG_referenced sample
    uint64_t aggr_us   = 100000;      // 100 ms aggregation
    uint64_t update_us = 1000000;     // 1 s region split/merge
    uint32_t min_nr_regions = 10;
    uint32_t max_nr_regions = 1000;
};
```

## Wire contract

```cpp
struct DamonRegion {
    uint64_t start_addr;
    uint64_t end_addr;
    uint32_t nr_accesses;     // accesses observed this aggregation window
    uint32_t age_aggrs;       // aggregation windows since last accessed
    uint64_t aggr_window_ts_ns;
};

class DamonReader {
    std::span<const DamonRegion> snapshot();   // current window
    std::span<const DamonRegion> history(N);   // last N windows
    // Find region containing addr:
    std::optional<DamonRegion> region_for(uintptr_t addr);
};
```

## Bench harness display

```
└─ damon: 47 regions  hot=2 (kvtable@0x7f3a... weights@0x7f4c...)  cold=12 promote-to-cxl?
```

## Cost model

- Kernel-side DAMON: 0.5-2% CPU at default settings (kernel docs cite ~0.3% with PTE access bit scanning, more with virtual-address-space targets).
- BPF tracepoint: ~50 ns × 1000 events/sec = 0.005% CPU.
- Userspace decode: similar.
- **Default-off**, opt-in via `CRUCIBLE_PERF_DAMON=1` for per-region access analysis runs.

## Known limits

- DAMON requires CONFIG_DAMON=y + CONFIG_DAMON_VADDR=y + CONFIG_DAMON_SYSFS=y (all default-y in mainline 5.15+, distro check needed).
- PG_referenced is a soft bit — regions accessed but with bit cleared by reclaim are missed.  Not a precise per-access counter.
- Region resolution depends on auto-split rate; bursty workloads can split slower than they shift.
- Aggregation window minimum is OS jiffies (~4 ms HZ=250); finer-grained sampling needs PmuSample DTLB instead.

## Sibling refs

- **Cold-path complement** to PmuSample's DTLB-miss IP sampling.  PmuSample = "where the access happened" (instruction); DamonReader = "what region was accessed how often" (data).
- **NUMA-balance input** for migration decisions — DAMON regions with high cross-node access predict bad placement.
- **CXL.mem tier promotion** — DamonReader output feeds the planned `Cipher` cold-tier-to-hot promotion decisions.
- **Bench harness**: complementary axis to the cycles/instructions story.
