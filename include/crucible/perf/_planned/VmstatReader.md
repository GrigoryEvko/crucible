# `VmstatReader` — `/proc/vmstat` comprehensive memory statistics

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/VmstatReader.h`.  Userspace-only.

## Problem

`/proc/vmstat` exposes ~120 monotonic kernel-side memory counters
(verified exists on 6.17).  Categories:

- **Allocation**: `pgalloc_dma32`, `pgalloc_normal`, ... per zone
- **Reclaim**: `pgsteal_kswapd`, `pgsteal_direct`, `pgscan_kswapd`, ...
- **Page faults**: `pgfault`, `pgmajfault`
- **Swap**: `pswpin`, `pswpout`
- **NUMA**: `numa_hit`, `numa_miss`, `numa_foreign`, ...
- **THP**: `thp_fault_alloc`, `thp_collapse_alloc`, `thp_split_*`
- **Compaction**: `compact_stall`, `compact_fail`, `compact_success`
- **Workingset**: `workingset_refault_*`, `workingset_activate_*`
- **OOM**: `oom_kill`

Single most-comprehensive memory observability source on Linux,
cheaper than any BPF program for the same data.  Crucible's
SenseHub already exposes a subset; this is the full surface.

## Mechanism

`/proc/vmstat` polled at 1 Hz.  Per-poll: ~50 µs (parse 120 lines).

## Wire contract

```cpp
struct VmstatSnapshot {
    // Map by counter name; 120+ keys.
    std::unordered_map<std::string, uint64_t> counters;
    uint64_t snapshot_ts_ns;

    // Convenience accessors:
    uint64_t pgfault_total();
    uint64_t pgmajfault_total();   // disk-loading faults
    uint64_t numa_miss_rate();     // numa_miss / (numa_hit+numa_miss)
    uint64_t thp_collapse_success_rate();
};
class VmstatReader {
    VmstatSnapshot snapshot();
    // Delta between two snapshots:
    VmstatDelta delta(const VmstatSnapshot& prev);
};
```

## Bench harness display

```
└─ vmstat: pgmajfault=0/s (no disk-loading)  numa_miss=2.3% (good locality)
           thp_collapse: 47/47 succeeded  workingset_refault=12 (cold reads)
```

## Cost model

- Per-poll: ~50 µs.
- 1 Hz cadence: 0.005% CPU.  Effectively free.

## Known limits

- Counters are kernel-version-specific; new counters added in 5.x/6.x.
  Tolerate unknown keys.
- Some counters reset on certain operations (e.g. compact_*); use
  delta-from-snapshot, not absolute, for window-scoped metrics.
- Per-cgroup vmstat is at `/sys/fs/cgroup/<path>/memory.stat` — separate
  facade `CgroupMemoryStatReader` (not currently planned; add if needed).

## Sibling refs

- **Comprehensive complement** to: `SenseHub` (which exposes ~12 vmstat
  counters as named slots — VmstatReader exposes the full ~120).
- **Joint reading** with: `BuddyinfoReader` (predicted vs actual alloc
  failures) + `vmscan_ext.bpf.c` (per-event reclaim attribution).
- **THP success-rate**: `thp_collapse_alloc / (thp_collapse_alloc +
  thp_collapse_alloc_failed)` is the canonical hugepage-health metric.
