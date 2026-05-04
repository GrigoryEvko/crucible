# `PmuCxl` — CXL fabric counters (kernel 6.6+)

**STATUS**: doc-only stub.  Tier-C (forward-looking).  Eventual path:
`include/crucible/perf/PmuCxl.h`.  Userspace-only.

## Problem

CXL (Compute Express Link) is the emerging memory + accelerator
fabric standard.  CXL.cache and CXL.mem devices expose their own
PMU counters via the kernel's `cxl_pmu` driver (kernel 6.6+).
Crucible deployments will increasingly include CXL memory tiers
(DRAM expansion, persistent memory, fabric-attached coherent storage)
where understanding fabric-side bandwidth + latency is the same kind
of essential as DRAM-channel BW today.

Forward-looking facade — most production fleets aren't on CXL.mem
yet (early 2026); but the CXL spec and Linux support are mature
enough to build the integration substrate now.

## Mechanism

```
/sys/bus/event_source/devices/cxl_pmu_mem<N>.<idx>/  — per-CXL-device PMU
                          (verified naming pattern from
                          drivers/perf/cxl_pmu.c devm_kasprintf call;
                          dot separator, not underscore — earlier
                          revision of this stub had it wrong)
events: M2S_REQ (memory-to-system requests), S2M_NDR_*, etc.
        per CXL.cache and CXL.mem traffic types
```

```cpp
int type = read_dynamic_pmu_type("/sys/bus/event_source/devices/cxl_pmu_mem0.0/type");
struct perf_event_attr attr{};
attr.type   = type;
attr.config = M2S_REQ;
attr.sample_period = 0;
```

Same mmap+RDPMC pattern as other uncore PMUs.

## API shape

```cpp
struct PmuCxlSnapshot {
    struct Device {
        uint64_t m2s_requests;       // memory-to-system (loads)
        uint64_t s2m_no_data_reps;   // S2M no-data responses (writes)
        uint64_t s2m_data_reps;      // S2M data responses
        uint8_t  device_id;
        char     pci_segment[8];
    };
    std::vector<Device> devices;
    [[nodiscard]] PmuCxlSnapshot operator-(...) const noexcept;
};
```

## Cost

- ~50-100 ns per device read.
- Most fleets: 0 CXL devices → load() returns nullopt fast.

## Known limits

- Kernel 6.6+ minimum.  Earlier kernels: load() returns nullopt.
- Hardware availability: production CXL.mem devices started shipping
  early 2025 (Intel Sapphire Rapids EE / AMD EPYC 9004 + select
  add-in cards: SK Hynix CMS-2, Samsung CMM-D, Micron CZ120).
- CXL.cache vs CXL.mem traffic separation: per-event counter.
  Cache traffic is opaque (transparent to OS); mem traffic is
  visible as memory accesses.
- Topology mapping (which CXL device backs which NUMA node) needs
  `/sys/bus/cxl/devices/...` parse at userspace; cache mapping at
  load().

## Sibling refs

- `PmuUncoreImc.md` — local DRAM BW; CXL adds a "remote DRAM" tier
- `iter_mmap.bpf.c` — per-VMA backing; CXL mappings show as memory
  on a different NUMA node
- Future GAPS task: cog/Cxl.h for per-Cog CXL discovery (#1263)
