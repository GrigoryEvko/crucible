# `PmuUncoreIio` — per-PCIe-stack throughput counters

**STATUS**: doc-only stub.  Tier-B.  Eventual path:
`include/crucible/perf/PmuUncoreIio.h`.  Userspace-only.

## Problem

PCIe traffic to GPUs / NVMe / NICs is invisible at the application
layer except via vendor SDKs (NVML, ROCm SMI, ethtool).  Per-PCIe-
stack uncore counters give a UNIFIED view: bytes/sec per direction
(card-to-host vs host-to-card), per stack (CPU socket might have
2-4 PCIe stacks).

Critical for diagnosing "which PCIe link is saturated" in
multi-GPU / multi-NIC nodes.

## Mechanism

Intel IIO PMU (Skylake-SP and later):
```
/sys/bus/event_source/devices/uncore_iio_*  — per-PCIe-stack
events: COMP_BUF_OCCUPANCY, DATA_REQ_OF_CPU, DATA_REQ_BY_CPU
        (read = card-to-host, write = host-to-card)
```

Each socket has 1-4 IIO instances depending on CPU SKU.

AMD: `amd_iommu_*` exposes IOMMU-mediated DMA counts; not as
fine-grained as Intel IIO but gives total GPU/NIC DMA bandwidth.

## API shape

```cpp
struct PmuUncoreIioSnapshot {
    struct Stack {
        uint64_t card_to_host_bytes;
        uint64_t host_to_card_bytes;
        uint8_t  socket;
        uint8_t  stack_id;
        char     pci_segment[8];   // e.g. "0000:5e"
    };
    std::vector<Stack> stacks;
    [[nodiscard]] PmuUncoreIioSnapshot operator-(...) const noexcept;
};
```

## Cost

- ~50 ns per stack read × 4-8 stacks per socket = ~200-400 ns total.

## Known limits

- Intel SKL-SP+, Cascade Lake, Ice Lake, Sapphire Rapids — different
  uarch event IDs.  Per-uarch table needed.
- AMD has a coarser view via `amd_iommu`; not per-stack but per-IOMMU.
- Mapping `stack_id` → physical PCIe slot requires correlating with
  `lspci -t` output at userspace.  Worth doing once at load() and
  caching the table.
- Per-card (per-BDF) granularity not exposed — only per-stack.  For
  per-card (e.g., GPU 0 vs GPU 1 on the same stack), use vendor SDK.

## Sibling refs

- `nvme_rq.bpf.c` — per-NVMe-command latency on top of which PCIe
  stack the NVMe sits
- `xdp_rx.bpf.c` — per-NIC packet rate; this gives PCIe-side BW
