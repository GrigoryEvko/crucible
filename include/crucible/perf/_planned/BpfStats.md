# `BpfStats` — observe our own BPF programs' kernel-side cost

**STATUS**: doc-only stub.  Tier-1 self-observation.  Eventual path:
`include/crucible/perf/BpfStats.h` + `src/perf/BpfStats.cpp`.
Userspace-only — NO custom BPF program (reads kernel stats about
EXISTING BPF programs).

## Problem

Crucible's BPF stack claims `<1% overhead`.  That claim is currently
**structurally unverifiable** — we have no facade that reads how much
CPU our N loaded BPF programs actually consume.  Each `bpf_prog__attach`
mortgages some kernel-side cycles per event; without measurement we
can't budget intelligently.

Concrete failure mode: enable a high-rate tracepoint program (e.g.
`filemap.bpf.c` at 100K events/sec on a busy host), and we have no way
to detect "this program just took 8% of one CPU" except through
indirect side-effects (workload regressed; SchedSwitch shows more
preemption).  By then it's already in production.

`BpfStats` reads per-BPF-program runtime accounting that the kernel
already maintains and exposes via two mechanisms.

## Mechanism

Two complementary read paths:

### Mechanism 1 — `BPF_OBJ_GET_INFO_BY_FD`

Per-program info struct includes `run_cnt` (calls) and `run_time_ns`
(cumulative on-CPU time in the BPF program).  Available via:

```c
struct bpf_prog_info info = {};
__u32 sz = sizeof(info);
bpf_obj_get_info_by_fd(prog_fd, &info, &sz);
// info.run_cnt + info.run_time_ns
```

Per-program access requires `CAP_BPF` (or `CAP_SYS_ADMIN` on older kernels).

### Mechanism 2 — `/proc/sys/kernel/bpf_stats_enabled`

Toggle that gates the kernel's accumulation of the above counters.
Default-off (because the per-event accounting itself costs ~10 ns).
Crucible's facade flips it on at startup, off at shutdown — opt-in via
`CRUCIBLE_PERF_BPFSTATS=1`.

### Mechanism 3 — `bpftool prog show -j`

Same data as Mechanism 1 but as JSON via `bpftool` (libbpf-managed).
Useful for cross-checking + for non-Crucible-loaded programs (third-
party agents on the same host).

## Reader cadence

Polled every 100 ms by Senses aggregator (or once per bench iteration
for bench harness).  Per-program delta = (run_cnt_now - run_cnt_prev,
run_time_ns_now - run_time_ns_prev).  Reports:
- `cycles_per_event` = run_time_ns / run_cnt (per-program latency)
- `cpu_pct` = run_time_ns_delta / wall_ns_delta × 100 (per-program CPU share)
- `events_per_sec` = run_cnt_delta / wall_sec_delta

## Cost model

- BPF stats accumulation (when enabled): ~10 ns per event for ALL programs (one extra timestamp read in dispatch).
- Reader-side polling: ~50 µs per program per poll (one syscall per program).  100 ms cadence × 10 programs = 5 ms/sec ≈ 0.5% CPU.
- Disabling stats restores zero-overhead path.

## Wire contract

Per-program snapshot struct:

```cpp
struct BpfProgStats {
    uint32_t prog_id;
    char     name[64];
    uint8_t  type;           // BPF_PROG_TYPE_*
    uint64_t run_cnt;
    uint64_t run_time_ns;
    uint64_t loaded_at_ns;
    uint32_t map_count;
    uint64_t snapshot_ts_ns; // wall clock at read
};
```

Returned as `std::span<const BpfProgStats>` from `BpfStats::snapshot()`.

## Bench harness display

```
└─ bpf_stats: pmu_sample=1.2% (47K ev/s × 254 ns) sched_switch=0.3% (8K ev/s × 380 ns)
              sense_hub=0.1% (50 ev/s × 1.8 µs) total=1.6%
```

## Known limits

- Kernel ≥ 5.1 (`bpf_stats_enabled` sysctl + `BPF_OBJ_GET_INFO_BY_FD` for runtime accounting).
- Counters reset only on program unload — accumulated since program load, not since facade init.  Facade rebases on first sample.
- Doesn't break down per-CPU; aggregate across all CPUs.  For per-CPU breakdown use a separate `BPF_MAP_TYPE_PERCPU_ARRAY` keyed by `bpf_get_smp_processor_id()` inside each program (program-side instrumentation).
- Doesn't measure verifier-rejected paths or BPF-to-BPF call chains (only entry-point cost).
- Doesn't see kernel-side wrapper cost (tracepoint dispatch, ring-buffer reservation) — only the BPF program body.

## Sibling refs

- **Self-observation triad** with: `PerfRecordObserver` (observes our perf_event ring sample loss + throttling) and `TracingSubscriberStats` (observes our tracepoint subscriber falling behind).
- **Replaces** the deleted `perf_throttle.bpf.c` reasoning — proper self-observation lives in this triad, not in BPF.
- **Senses aggregator** (planned) folds this into the per-bench-window summary so every bench prints "what observability cost itself".

## Why this matters strategically

Without this facade, Crucible cannot validate its own design claims
about overhead.  Every other observability stub assumes the cost model
holds; `BpfStats` is the empirical witness.  Ship FIRST among the
self-observation tier.
