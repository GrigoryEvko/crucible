# `CpuidleReader` — `/sys/devices/system/cpu/cpu*/cpuidle/` C-state residencies

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/CpuidleReader.h`.  Userspace-only.

## Problem

C-states (CPU idle states) save power but cost wakeup latency:

| State | Latency | Wake cost |
|-------|---------|-----------|
| C0/POLL | 0 µs | 0 |
| C1 | <1 µs | tiny |
| C1E | ~5 µs | small |
| C2 | ~10 µs | moderate |
| C3 | ~50 µs | high |
| C6 | ~150 µs | very high |
| Package C6 | ~300 µs | severe |

Per-CPU per-state residency (verified exists at
`/sys/devices/system/cpu/cpu0/cpuidle/state0/{name,time,usage,latency}`):
```
state0/name         POLL
state0/time         0          # microseconds in this state
state0/usage        0          # entry count
state0/latency      0          # ns wakeup latency (advertised)
state1/name         C1
state1/time         24500000
state1/usage        45123
state1/latency      1
state2/name         C2
state2/time         123456789
state2/usage        12345
state2/latency      5
```

Bench-relevance: high C-state residency between bench iterations →
high wake-up latency → first iteration of next window much slower.

For pure aggregate "what % of time were we in deep C-states" the
polling read is much cheaper than `cpu_idle.bpf.c` BPF attachment.

## Mechanism

Per-CPU per-state file polling at 1 Hz.  Per-poll: ~50 µs (4 files ×
3 states × N cpus).

## Wire contract

```cpp
struct CpuidleState {
    char     name[16];
    uint64_t time_us;        // accumulated time in this state
    uint64_t entry_count;
    uint32_t latency_ns;     // advertised wakeup latency
};
struct CpuidleSnapshot {
    uint32_t cpu;
    std::span<const CpuidleState> states;
    uint64_t snapshot_ts_ns;
};
class CpuidleReader {
    std::span<const CpuidleSnapshot> snapshot();
    // Aggregate:
    double pct_in_deep_idle();   // fraction of CPUs in C3+ at last sample
};
```

## Bench harness display

```
└─ cpuidle: cpu0 96% C0 (busy)  cpu7 87% C2 (going deep)
            avg_wake_latency=12 µs
```

## Cost model

- Per-poll: ~50 µs.
- 1 Hz cadence: 0.005% CPU.  Effectively free.

## Known limits

- Snapshot only; entry/exit timing requires `cpu_idle.bpf.c` BPF.
- Advertised latency is firmware-reported; actual wake latency can
  differ — measure via PmuSample latency tail.
- Pre-bench warmup (`cpupower idle-set -d N` to disable deep states)
  is the production fix; this reader detects when it wasn't applied.

## Sibling refs

- **Aggregate complement** to: `cpu_idle.bpf.c` (per-transition timing).
- **Joint reading** with: `MsrAperfMperfReader` — high deep-idle
  residency + low aperf/mperf util = expected; low residency + low util
  = something blocking us in C0.
- **Bench-reliability**: deep-C-state residency > 50% during bench is
  auto-banner ("disable deep idle for tail-latency benches").
