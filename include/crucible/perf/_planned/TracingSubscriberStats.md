# `TracingSubscriberStats` — observe when our tracepoint subscriber falls behind

**STATUS**: doc-only stub.  Tier-1 self-observation.  Eventual path:
`include/crucible/perf/TracingSubscriberStats.h`.  Userspace-only —
NO BPF, NO perf_event.  Just `/sys/kernel/debug/tracing/...` polling.

## Problem

When Crucible attaches a BPF tracepoint program (e.g. `sched_switch`,
`io_uring`, `block_rq`), the kernel writes events into a per-CPU ring
buffer that BPF reads.  If our BPF program (or our userspace consumer
of a `BPF_MAP_TYPE_RINGBUF`) falls behind the producer rate, the
kernel **silently drops events** — and we have no facade today that
detects when this happens.

Manifestation: bench reports "the syscall_latency facade saw 50K events
this iteration".  In reality, 75K events fired and 25K were dropped.
Augur draws conclusions from a truncated stream.

## Mechanism

The kernel tracing subsystem exposes per-CPU ring buffer statistics
at `/sys/kernel/debug/tracing/per_cpu/cpuN/stats`.  Verified format
on kernel 6.17:

```
entries: 0
overrun: 0
commit overrun: 0
bytes: 0
oldest event ts:     0.000000
now ts: 838257.366523
dropped events: 0
read events: 0
```

Key fields:
- **`entries`** — events currently buffered (instantaneous depth).
- **`overrun`** — events dropped because producer overran consumer (load-bearing).
- **`commit overrun`** — events dropped during write commit.
- **`dropped events`** — total events dropped this CPU.
- **`bytes`** — bytes consumed.

Polled at 1 Hz (cold).  Per-CPU delta = (overrun_now - overrun_prev,
dropped_events_now - dropped_events_prev).  Aggregate across CPUs.

For BPF ringbuf consumers, additional kernel-exposed stats via
`BPF_OBJ_GET_INFO_BY_FD` on the map fd (info struct includes
`cur_entries` proxy; full counters via `bpftool map dump` of internal
stats map when CONFIG_DEBUG_INFO_BTF=y).

## Per-program subscriber lag

For each Crucible-loaded BPF program, additionally compute:
- **`producer_rate`** = events/sec into the ring (from BPF stats, see
  `BpfStats.md` — that facade reports `run_cnt` deltas).
- **`consumer_rate`** = events/sec drained by userspace (from per-program
  drain counters Crucible already maintains).
- **`lag` = producer - consumer** — > 0 means falling behind.

## Reader cadence

Polled every 1 second (cold path).  10 CPUs × 1 file read × 1 Hz =
10 stat reads/sec ≈ 50 µs/sec ≈ 0.005% CPU.  Free.

## Wire contract

```cpp
struct TracingSubscriberStats {
    struct PerCpu {
        uint32_t cpu;
        uint64_t entries_now;
        uint64_t overrun_total;
        uint64_t commit_overrun_total;
        uint64_t dropped_events_total;
        uint64_t bytes_total;
        uint64_t snapshot_ts_ns;
    };
    std::span<const PerCpu> per_cpu;

    struct PerProgram {
        uint32_t prog_id;
        uint64_t producer_rate_hz;
        uint64_t consumer_rate_hz;
        int64_t  lag;       // producer - consumer
    };
    std::span<const PerProgram> per_program;
};
```

## Bench harness display

```
└─ tracing_lag: cpu0..cpu7 OK  cpu8: dropped=132 events (sched_switch falling behind)
                io_uring: producer=80K/s consumer=78K/s lag=2K/s ← REPORT PARTIAL
```

Any drop in any CPU during the bench window invalidates conclusions
drawn from that CPU's events; flag inline.

## Cost model

Pure userspace polling, no kernel cost.  Per-poll: ~5 µs (file open +
read + parse + close × N cpus).  At 1 Hz cadence: <0.01% CPU.

## Known limits

- `/sys/kernel/debug/tracing/per_cpu/cpuN/stats` requires CAP_SYS_ADMIN (or debugfs+tracefs world-readable, distro-dependent).  Crucible's Keeper runs as a service; access is normal.
- Counters are kernel-side cumulative since boot.  Reset only on tracing-clear.  Facade rebases on first read.
- The "subscriber" abstraction is per-tracepoint, not per-program — multiple programs sharing one tracepoint share the queue.  Per-program lag is a derived metric from BpfStats deltas.
- Doesn't cover perf_event ring overflow (that's `PerfRecordObserver`'s `PERF_RECORD_LOST`).  Different ring, different mechanism.

## Sibling refs

- **Self-observation triad** with: `BpfStats` (per-program cost) and `PerfRecordObserver` (perf_event ring overflow).
- **Senses aggregator** (planned) folds this into bench window summary.
- Without this facade, EVERY tracepoint-based facade silently lies under load.
