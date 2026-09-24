# `CpufreqReader` — `/sys/devices/system/cpu/cpufreq/` policy + state reader

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/CpufreqReader.h`.  Userspace-only.

## Problem

CPU frequency state (governor + current target) is the SECOND-most-
load-bearing CPU observation after `MsrAperfMperfReader` (which gives
TRUE delivered freq).  This reader gives:

- **Governor**: which algorithm is choosing the freq (`performance`,
  `schedutil`, `powersave`, `ondemand`)
- **Scaling target frequency**: what governor asked for
- **Min/max bounds**: hardware/policy clamps
- **Driver name**: `acpi_cpufreq` / `intel_pstate` / `amd_pstate` / ...

Bench-relevance: a wrong governor (default `schedutil` instead of
`performance`) is the #1 source of bench variance.  Detecting it at
bench-window start lets the harness banner-warn AND record exactly
what the run was using for reproducibility.

## Mechanism

Per-CPU `/sys/devices/system/cpu/cpufreq/policy*/`:
```
scaling_governor          performance
scaling_cur_freq          5234567
scaling_min_freq          3000000
scaling_max_freq          5500000
cpuinfo_cur_freq          5234567
cpuinfo_min_freq          400000
cpuinfo_max_freq          5500000
scaling_driver            amd-pstate-epp
energy_performance_preference  performance
```

Polled at 1 Hz (cold).  Per-poll: ~30 µs (multiple file reads × N policies).

## Wire contract

```cpp
struct CpufreqPolicy {
    char     governor[16];
    char     driver[32];
    char     epp[16];                   // energy_performance_preference
    uint64_t scaling_target_khz;        // current target
    uint64_t scaling_min_khz;
    uint64_t scaling_max_khz;
    uint64_t cpuinfo_max_khz;           // hardware limit
    std::span<const uint32_t> cpus;     // CPUs sharing this policy
};
class CpufreqReader {
    std::span<const CpufreqPolicy> snapshot();
    // Cross-check:
    bool all_at_performance_governor();
};
```

## Bench harness display

```
└─ cpufreq: gov=performance  driver=amd-pstate-epp  epp=performance
            target=5.2 GHz  max=5.5 GHz  (delivered=5.0 GHz per MsrAperfMperf)

[!]  cpufreq: gov=schedutil — bench results will be variance-prone;
              recommend `cpupower frequency-set -g performance` before bench
```

## Cost model

- Per-poll: ~30 µs.
- 1 Hz cadence: 0.003% CPU.  Effectively free.

## Known limits

- `scaling_cur_freq` may LIE under turbo (returns governor's target,
  not delivered).  Use `MsrAperfMperfReader` for ground truth.
- Per-policy enumeration discovers shared groups (SMT siblings often
  share a policy on AMD, single-CPU policies on Intel post-Skylake).
- CPU hotplug invalidates discovered policy paths — re-enumerate on
  `cpu_hotplug.bpf.c` events (or just at bench-window start).

## Sibling refs

- **Ground-truth complement** to: `MsrAperfMperfReader` (delivered freq).
  CpufreqReader = "what governor asked for"; MsrAperfMperf = "what
  actually got delivered".  Discrepancy = thermal cap or power cap.
- **Pair with**: `power_amd_pstate.bpf.c` (transition-event attribution).
- **Bench reliability**: governor-not-`performance` is auto-banner.
