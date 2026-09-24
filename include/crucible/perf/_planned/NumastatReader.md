# `NumastatReader` — `/sys/devices/system/node/node*/numastat` per-node memory stats

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/NumastatReader.h`.  Userspace-only.

## Problem

NUMA placement is the single highest-leverage optimization on
multi-socket hosts (4 GHz vs 2.5 GHz effective DRAM bandwidth).

`/sys/devices/system/node/node*/numastat` (verified exists on this host)
exposes per-node:

```
numa_hit         <count>   # alloc satisfied from preferred node
numa_miss        <count>   # alloc satisfied from non-preferred (forced)
numa_foreign     <count>   # alloc was preferred-elsewhere but landed here
interleave_hit   <count>   # interleave-policy alloc landed on this node
local_node       <count>   # alloc satisfied from same-node-as-task
other_node       <count>   # alloc satisfied from cross-node
```

Bench-relevance: `local_node / (local_node + other_node)` is the
canonical NUMA-locality metric.  <80% → tasks are migrating away
from their data; pin or rebalance.

`numa_foreign` rising = pressure on this node forcing other tasks'
allocations to overflow here.

## Mechanism

Per-node file at `/sys/devices/system/node/node0/numastat`,
`/sys/devices/system/node/node1/numastat`, etc.  Polled at 1 Hz.

Per-poll: ~10 µs × N nodes (typically 1-8 nodes).

## Wire contract

```cpp
struct NumastatNode {
    uint8_t  node_id;
    uint64_t numa_hit;
    uint64_t numa_miss;
    uint64_t numa_foreign;
    uint64_t interleave_hit;
    uint64_t local_node;
    uint64_t other_node;
    uint64_t snapshot_ts_ns;
};
class NumastatReader {
    std::span<const NumastatNode> snapshot();
    // Aggregate metrics:
    double locality_fraction();      // local_node / (local + other)
    double foreign_pressure();        // numa_foreign rate increase
};
```

## Bench harness display

```
└─ numa: locality=94% (good)  node0 hit/miss=987K/12K  node1 hit/miss=843K/45K
         node1 numa_foreign=high — node0 OOM pressure forcing node1 fallback?
```

## Cost model

- Per-poll: ~10 µs × N nodes.
- 1 Hz × 8 nodes: ~80 µs/sec ≈ 0.008% CPU.  Effectively free.

## Known limits

- Aggregate by-node only; per-process attribution requires `numastat
  -p <pid>` (`/proc/<pid>/numa_maps`) — separate facade
  `ProcNumaMapsReader` (not currently planned).
- `numa_miss` rate alone doesn't say where misses LANDED; pair with
  `numa_balance.bpf.c` for migration attribution.

## Sibling refs

- **NUMA-balance pair** with: `numa_balance.bpf.c` (per-task
  migration attribution).
- **NUMA-locality metric**: complementary to `iter_task` (planned)
  per-task placement; combined gives "task X on node Y but its data
  is on node Z".
- **PMU complement**: `PmuUncoreFabric` (UPI/Infinity Fabric per-link
  bandwidth) shows the cross-socket TRAFFIC; numastat shows the
  cause (high other_node = high cross-socket).
