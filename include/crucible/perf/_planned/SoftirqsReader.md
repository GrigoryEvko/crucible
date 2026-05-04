# `SoftirqsReader` — `/proc/softirqs` per-CPU softirq counts per vector

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/SoftirqsReader.h`.  Userspace-only.

## Problem

Softirqs are deferred-from-IRQ kernel work running in softirq
context (still kernel time, runs from `do_softirq()` after a hard
IRQ handler returns or from `ksoftirqd` kernel threads under load).
Categories on x86: HI, TIMER, NET_TX, NET_RX, BLOCK, IRQ_POLL,
TASKLET, SCHED, HRTIMER, RCU.

Per-CPU per-category counts answer: "is NET_RX dominating CPU N?"
"is TIMER softirq spike correlated with our latency increase?"

`SenseHub` already exposes aggregate `SOFTIRQ_STOLEN_NS`; this
reader gives the per-vector breakdown at zero BPF cost.

## Mechanism

`/proc/softirqs` (verified exists on 6.17):
```
                    CPU0       CPU1       CPU2  ...
          HI:          0          0          0
       TIMER:    1234567    1234560    1234555
      NET_TX:        100         99         98
      NET_RX:    9876543    9876200    9876100
       BLOCK:      45123      44987      44900
    IRQ_POLL:          0          0          0
     TASKLET:        100         99         98
       SCHED:    2345678    2345600    2345500
     HRTIMER:     567890     567800     567700
         RCU:    3456789    3456700    3456600
```

Polled at 1 Hz.  Per-poll: ~30 µs.

## Wire contract

```cpp
enum class SoftirqVec { HI, TIMER, NET_TX, NET_RX, BLOCK, IRQ_POLL,
                        TASKLET, SCHED, HRTIMER, RCU };

struct SoftirqsSnapshot {
    uint32_t cpu;
    std::array<uint64_t, 10> per_vector_count;
    uint64_t snapshot_ts_ns;
};
class SoftirqsReader {
    std::span<const SoftirqsSnapshot> snapshot();
    uint64_t per_cpu_total(uint32_t cpu);
};
```

## Bench harness display

```
└─ softirqs: NET_RX=2.1M/s (cpu0,1,2 — RX-heavy)
             TIMER=4K/s (LOC, normal)  RCU=1.2K/s
             SCHED=8K/s (high — preemption-heavy?)
```

## Cost model

- Per-poll: ~30 µs.
- 1 Hz cadence: 0.003% CPU.  Effectively free.

## Known limits

- 10 fixed vectors; parser pinned to the well-known set.
- Doesn't separate "ran in interrupt context immediately" from "ran
  in ksoftirqd thread later" — same counter.  For that distinction
  use `hardirq.bpf.c` (entry-pair timing) + ksoftirqd presence in
  `iter_task` (planned).
- Per-vector is global; per-process attribution requires ksoftirqd
  scheduling stats (NET_RX work running on ksoftirqd preempts our
  task only if that ksoftirqd shares the CPU).

## Sibling refs

- **Aggregate complement** to: SenseHub `SOFTIRQ_STOLEN_NS` (total
  softirq time stolen from our task).
- **NET_RX correlator**: high NET_RX softirq + high `napi_poll.bpf.c`
  fire rate → kernel can't keep up with NIC.
- **RCU correlator**: high RCU softirq + long `rcu_gp.bpf.c` grace
  periods → RCU callback queue backed up.
