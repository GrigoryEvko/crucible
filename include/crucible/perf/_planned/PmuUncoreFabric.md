# `PmuUncoreFabric` — UPI / Infinity Fabric per-link cross-socket traffic

**STATUS**: doc-only stub.  Tier-B.  Eventual path:
`include/crucible/perf/PmuUncoreFabric.h`.  Userspace-only.

## Problem

Multi-socket nodes pay for cross-socket traffic over UPI (Intel) or
Infinity Fabric (AMD) — every cache line that misses local L3 and
hits remote-socket cache costs ~100 ns + fabric BW.  Per-link
saturation is invisible at the application layer; only the uncore
PMU surfaces it.

Especially relevant for ML allreduces that span both sockets, and
NUMA-misplaced workloads.

## Mechanism

Intel UPI:
```
/sys/bus/event_source/devices/uncore_upi_0/  — per-link UPI PMU
events: UPI_DATA_BANDWIDTH_TX, UPI_DATA_BANDWIDTH_RX
```

AMD Infinity Fabric:
```
/sys/bus/event_source/devices/amd_df/        — Data Fabric PMU
events: 0x07f { read_request_per_link }
```

```cpp
struct perf_event_attr attr{};
attr.type   = read_dynamic_pmu_type("/sys/.../uncore_upi_0/type");
attr.config = ...;  // event ID per uarch
```

## API shape

```cpp
struct PmuUncoreFabricSnapshot {
    struct Link {
        uint64_t bytes_tx;
        uint64_t bytes_rx;
        uint8_t  src_socket;
        uint8_t  dst_socket;
    };
    std::vector<Link> links;
    [[nodiscard]] PmuUncoreFabricSnapshot operator-(...) const noexcept;
    [[nodiscard]] uint64_t cross_socket_bytes() const noexcept;
};

class PmuUncoreFabric {
public:
    [[nodiscard]] static std::optional<PmuUncoreFabric>
        load(::crucible::effects::Init) noexcept;
    [[nodiscard]] PmuUncoreFabricSnapshot read() const noexcept;
};
```

## Cost

- ~50 ns per link read.  Modern 4-socket Cascade Lake = 6 UPI links
  (3 per socket pair, full mesh) → 300 ns.

## Known limits

- Per-vendor config: Intel UPI ≠ AMD IF ≠ ARM CMN-700 mesh.
  Vendor-discriminated impl.
- Single-socket systems: link count = 0; load() returns nullopt with
  "no fabric to measure" diagnostic.
- AMD IF link enumeration is murky; Linux exposes `amd_df` as one
  PMU but not per-link cleanly.  Fall back to "total fabric BW"
  rather than per-link on AMD.

## Sibling refs

- `PmuUncoreImc.md` — local DRAM BW; this is cross-socket BW
- `iter_task.bpf.c` — per-task NUMA placement; correlate with cross-
  socket cost to spot misplacement
