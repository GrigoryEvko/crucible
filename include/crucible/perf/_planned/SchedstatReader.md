# `SchedstatReader` — `/proc/schedstat` per-CPU + per-domain scheduling stats

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/SchedstatReader.h`.  Userspace-only —
just file polling.

## Problem

Verified format on kernel 6.17:
```
version 17
timestamp <jiffies>
cpu0 <yld_count> <sched_count> <sched_goidle> <ttwu_count> <ttwu_local>
     <runtime_ns> <wait_ns> <slice_count>
domain0 SMT 00010001 <load_balance counters x ~24>
domain1 MC  00ff00ff <load_balance counters x ~24>
...
```

Per-CPU fields directly answer:
- **`runtime_ns`** — total time tasks ran on this CPU
- **`wait_ns`** — total time tasks waited in run-queue (load-balance latency)
- **`ttwu_count`** / **`ttwu_local`** — try-to-wake-up count, local-vs-remote split

Per-domain (NUMA / cache) fields expose load-balance attempts +
successes per scheduler domain — early warning when load-balance
falls behind.

`SchedSwitch` BPF gives per-task off-CPU duration; this reader gives
the AGGREGATE picture per CPU + per scheduling domain at zero BPF cost.

## Mechanism

```cpp
auto fp = std::ifstream{"/proc/schedstat"};
// Parse line-by-line; "cpu" lines and "domain" lines.
// Aggregate per-CPU + per-domain into snapshot.
```

Polled at 1 Hz.  Per-poll: ~50 µs (file open + read + parse + close).

## Wire contract

```cpp
struct SchedstatPerCpu {
    uint32_t cpu;
    uint64_t yld_count;
    uint64_t sched_count;
    uint64_t sched_goidle;          // entered idle
    uint64_t ttwu_count;
    uint64_t ttwu_local;            // wake on same CPU (cache-warm)
    uint64_t runtime_ns;
    uint64_t wait_ns;
    uint64_t slice_count;
    uint64_t snapshot_ts_ns;
};
struct SchedstatPerDomain {
    uint32_t cpu_anchor;
    uint8_t  domain_idx;            // 0=SMT, 1=MC, 2=NUMA, ...
    char     domain_name[16];
    uint64_t load_balance_count;    // attempts
    uint64_t load_balance_failed;
    uint64_t load_balance_imbalance;
    uint64_t alb_count;             // active-load-balance fired
    // ~20 more per-domain counters
};
class SchedstatReader {
    std::span<const SchedstatPerCpu>     per_cpu();
    std::span<const SchedstatPerDomain>  per_domain();
};
```

## Bench harness display

```
└─ schedstat: avg_wait=120ns  ttwu_remote=4% (good cache locality)
              load_balance: SMT=98% local  MC=87% local  NUMA=12% local (bad!)
```

NUMA load-balance "local <50%" indicates fleet should pin or rebalance.

## Cost model

- Per-poll: ~50 µs file read + ~50 µs parse for 16 CPUs = ~100 µs.
- 1 Hz cadence: 0.01% CPU.  Effectively free always-on.

## Known limits

- /proc/schedstat fields are kernel-version-specific; check `version` field
  (currently v17 on 6.17) and adapt parser per-version.
- Per-domain field count varies with CONFIG_SCHED_AUTOGROUP / CONFIG_NUMA;
  parser must tolerate variable column counts.
- Doesn't break down per-cgroup or per-task — for that use `SchedSwitch`
  BPF program (which DOES give per-task) or `iter_cgroup` (planned).

## Sibling refs

- **Aggregate complement** to: `SchedSwitch` BPF (per-task off-CPU durations).
- **NUMA-imbalance** alarm input — when domain2 (NUMA) load-balance is
  failing, paired with `numa_balance.bpf.c` for per-task migration data.
- **PsiReader** sibling: PSI gives "is this CPU pressured", schedstat
  gives "by how much / from where".
