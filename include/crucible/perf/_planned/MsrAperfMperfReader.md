# `MsrAperfMperfReader` — true delivered CPU frequency via aperf/mperf

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/MsrAperfMperfReader.h`.  Userspace-only
via `msr` perf_event PMU (verified on this host:
`/sys/bus/event_source/devices/msr/events/{aperf,mperf,tsc,irperf}`).

## Problem

`/sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq` and
`tracepoint/power/cpu_frequency` both lie under turbo: they report
the **target** P-state's frequency, not the **delivered** frequency.

Real example: governor sets P-state to "max boost = 5.0 GHz", but
the CPU is actually running at 4.2 GHz because (a) thermal headroom
exhausted, (b) power budget capped, (c) all-core boost is lower than
single-core boost.  Cycles-per-bench-iteration silently changes by
20% with no signal in the cpufreq tracepoint.

The TRUE delivered frequency is computed from the canonical formula:

```
delivered_freq = base_freq × (Δaperf / Δmperf)
```

Where APERF / MPERF are AMD/Intel MSRs that count "actual" and
"maximum" performance cycles respectively (both increment at base
frequency when CPU is fully utilized; APERF scales with frequency
boost).

This is the SINGLE most-load-bearing CPU-state observation for
ML/HPC bench reliability.

## Mechanism

The `msr` PMU (`/sys/bus/event_source/devices/msr`) exposes 4 events
on this host (verified 2026-05-04):

```
/sys/bus/event_source/devices/msr/events/
    aperf       — APERF MSR delta
    mperf       — MPERF MSR delta
    tsc         — Time Stamp Counter delta
    irperf      — Instructions retired (AMD)
```

(Intel exposes `smi` instead of `irperf`; both vendors expose
aperf/mperf/tsc as the standard set.)

Open as a perf_event group (read all 4 atomically per CPU):

```cpp
struct perf_event_attr attr = {};
attr.type = read_msr_pmu_type();   // from /sys/bus/event_source/devices/msr/type
attr.config = APERF_EVENT_CONFIG;
attr.exclude_idle = 1;
// open APERF, then MPERF/TSC/IRPERF in same group
int aperf_fd = perf_event_open(&attr, /*pid=*/ -1, /*cpu=*/ N, /*group=*/ -1, 0);
// ... open the rest with group_fd = aperf_fd
```

Read at bench-window boundaries with `read(group_fd, buf, size)` —
single syscall returns all 4 deltas atomically.  Or use RDPMC if the
kernel exposes it for msr PMU events (depends on `perf_event_paranoid`).

## Reader cadence

Polled at bench-window boundaries (1-1000 Hz depending on bench
inner-loop length).  Per-poll: ~5 µs (one read syscall × N cpus, or
~50 ns × N cpus via RDPMC).

## Wire contract

```cpp
struct MsrCounterSnapshot {
    uint32_t cpu;
    uint64_t aperf_delta;       // since last snapshot
    uint64_t mperf_delta;
    uint64_t tsc_delta;
    uint64_t irperf_delta;      // AMD; smi_count on Intel
    double   delivered_freq_hz; // computed: base × (aperf/mperf)
    double   utilization_frac;  // computed: mperf / tsc (1.0 = always-busy)
    uint64_t snapshot_ts_ns;
};
class MsrAperfMperfReader {
    std::span<const MsrCounterSnapshot> snapshot();  // per-CPU
    // Aggregate stats:
    double mean_delivered_freq_hz();
    double min_delivered_freq_hz();
};
```

## Bench harness display

```
└─ cpu_freq: cpu0 4.83 GHz (97% util)  cpu7 5.21 GHz (98% util)
              cpu_avg=4.91 GHz min=3.20 GHz (cpu14 thermal-capped?)
```

A delivered-vs-target gap > 5% during a bench window flags the
iteration's variance as suspect.

## Cost model

- One-time setup: ~100 µs (perf_event_open × 4 × N cpus).
- Per-poll: ~5 µs syscall path or ~50 ns RDPMC path.
- At 100 Hz poll, 16 cpus, syscall path: 8 ms/sec ≈ 0.8% CPU.  Acceptable.
- At 100 Hz poll, 16 cpus, RDPMC path: 80 µs/sec ≈ 0.008% CPU.  Effectively free.

## Known limits

- aperf/mperf are reset on suspend/resume; pre-S3-bench measurements
  invalidated.  Detect via TSC discontinuity.
- AMD ZenN+ adds `IRPERF` (instructions retired); Intel uses `SMI` slot
  for SMI count (rare event indicating SMM mode entry, also bench-relevant).
- MSR PMU access requires `CAP_SYS_ADMIN` or `perf_event_paranoid ≤ 0`
  (default 2 on Fedora; need bench-time tuning OR run as root).
- Per-thread vs per-CPU: msr PMU is per-CPU only; cannot pin to a
  specific tid.  For per-thread frequency use ALSO PmuCounters' CYCLES
  counter pinned to tid.

## Sibling refs

- **Bench-reliability complement** to: `power_amd_pstate.bpf.c` (when
  the kernel CHANGED P-state) and `cpufreq` tracepoint (TARGET freq).
  This stub gives DELIVERED freq — the ground truth.
- **Joint reading** with `PmuCounters` (cycles, instructions): IPC =
  `instructions / cycles`; cycles = aperf × ratio (already delivered);
  this stub anchors IPC to wall-clock time correctly.
- **Cross-vendor**: AMD + Intel both expose this PMU identically.
  ARM equivalent uses different MSRs (CNTPCT_EL0, CNTFRQ_EL0) — separate
  facade if/when ARM target matters.
