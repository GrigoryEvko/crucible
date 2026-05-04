# `PmuUncoreImc` — per-socket DRAM channel bandwidth

**STATUS**: doc-only stub.  Tier-B.  Eventual path:
`include/crucible/perf/PmuUncoreImc.h`.  Userspace-only.

## Problem

DRAM bandwidth is the most-saturated resource on most ML / HPC nodes.
Per-socket per-channel bandwidth tells you "channel 0 of socket 0 is
hot" → memory-controller bottleneck rather than CPU-bound.

## Mechanism

Linux exposes IMC (integrated memory controller) counters as a per-
socket dynamic perf PMU.  Each socket has 1-2 IMC instances, each
with 4-8 channels.

```cpp
// Per memory controller (typically uncore_imc_0, _1, ... per socket):
int type = read_dynamic_pmu_type("/sys/bus/event_source/devices/uncore_imc_0/type");
struct perf_event_attr attr{};
attr.type   = type;
attr.config = 0x04;  // CAS_COUNT_RD (reads)  — varies by uarch
                     // 0x0c = CAS_COUNT_WR (writes)
attr.sample_period = 0;
// Open per-CPU: perf_event_open(&attr, -1, cpu, ...) where cpu must
// be on the IMC's NUMA node (kernel rejects otherwise).
```

Bandwidth = `(read_count + write_count) × 64 B / interval_ns × 1e9`.

## API shape

```cpp
struct PmuUncoreImcSnapshot {
    struct Channel {
        uint64_t read_count;   // CAS reads
        uint64_t write_count;  // CAS writes
    };
    // Sized at load() based on detected (sockets × channels).
    std::vector<Channel> channels;

    [[nodiscard]] PmuUncoreImcSnapshot operator-(const PmuUncoreImcSnapshot&) const noexcept;
    [[nodiscard]] uint64_t total_bytes_per_sec(uint64_t interval_ns) const noexcept;
};

class PmuUncoreImc {
public:
    [[nodiscard]] static std::optional<PmuUncoreImc> load(::crucible::effects::Init) noexcept;
    [[nodiscard]] PmuUncoreImcSnapshot read() const noexcept;
    [[nodiscard]] uint32_t num_channels() const noexcept;  // sockets × channels
};
```

## Cost

- `read()`: ~50 ns × N_channels.  Typical 16-channel system → ~800 ns
  read.

## Known limits

- Vendor-specific config bits.  Intel client (Skylake+): CAS_COUNT_*.
  Intel server (Cascade Lake+): different MSRs.  AMD Zen: UMC PMU
  (`amd_umc_*`).  Per-vendor config table needed.
- Requires CAP_PERFMON or paranoid <= 0 (uncore is system-wide).
- Bytes/CAS = 64 (cache line size) on x86_64.  ARM may differ; use
  `/sys/bus/event_source/devices/uncore_imc_0/format/event` for size.
- Per-NUMA-node placement matters: open the CPU corresponding to the
  IMC's local socket; otherwise kernel rejects.

## Sibling refs

- `vmscan_ext.bpf.c` — kswapd steals DRAM BW; cross-correlate
- `PmuCmtMbm.md` — per-process bandwidth; this one is system-wide
- `PmuUncoreFabric.md` — UPI/IF fabric (cross-socket); orthogonal axis
