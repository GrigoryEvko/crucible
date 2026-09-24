# `BuddyinfoReader` — `/proc/buddyinfo` page allocator order distribution

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/BuddyinfoReader.h`.  Userspace-only.

## Problem

The kernel's buddy allocator maintains free-page lists per zone per
order (0=4KB, 1=8KB, 2=16KB, ..., 10=4MB).  When a higher-order
allocation is needed (e.g. order-3 for 32KB hugetlbfs precursor or
order-4 for jumbo frames), the buddy allocator searches up the
order list — if no order-N pages are free, it falls back to compaction
or splits a larger one.

Buddyinfo answers: "how close is each zone to higher-order exhaustion?"
Critical for hugepage-using workloads (Crucible's MetaLog uses
hugepages — see WRAP-MetaLog-5).

Format on 6.17:
```
Node 0, zone      DMA      1      1      0      0      0      1      0      0      0      1      3
Node 0, zone    DMA32     12      9     11      8      6     11      9      8      6      4      8
Node 0, zone   Normal   3409   1876   1234    789    432    234    123     67     34     12      4
```

Per-row: node, zone, free-count at order 0, 1, 2, ..., 10.

## Mechanism

`/proc/buddyinfo` polled at 1 Hz.  Per-poll: ~10 µs (file is small).

## Wire contract

```cpp
struct BuddyZone {
    uint8_t  node;
    char     zone[16];                       // DMA / DMA32 / Normal / Movable
    std::array<uint64_t, 11> free_per_order; // order 0..10
    uint64_t snapshot_ts_ns;
};
class BuddyinfoReader {
    std::span<const BuddyZone> snapshot();
    // Predict: can we still allocate N pages of order K?
    bool can_allocate(uint8_t node, uint8_t order, uint32_t count);
};
```

## Bench harness display

```
└─ buddy: Node0/Normal high-order=ok  Node1/Normal order≥4 nearly empty
          (next THP collapse may stall on compaction)
```

## Cost model

- Per-poll: ~10 µs (small file).
- 1 Hz cadence: 0.001% CPU.  Effectively free.

## Known limits

- Snapshot only; concurrent allocations between polls invisible.  For
  per-allocation attribution use `page_allocator.bpf.c` (planned).
- Doesn't predict reclaim / compaction failure modes — those need
  `vmscan_ext.bpf.c` + `thp_ext.bpf.c` (planned).
- Multi-zone systems (NUMA + DMA + DMA32 + Normal) have multiple rows
  per node; aggregate carefully.

## Sibling refs

- **Predictive complement** to: `page_allocator.bpf.c` (per-alloc
  attribution).  Buddyinfo says "you'll fail soon"; page_allocator
  says "you just failed and here's why".
- **Hugepage planning**: order-9 (2MB THP) + order-10 (4MB) free
  count is the predictor for THP collapse success.
