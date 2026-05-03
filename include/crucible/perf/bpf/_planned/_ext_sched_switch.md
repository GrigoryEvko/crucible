# Extension: `sched_switch.bpf.c` — cycle attribution per off-CPU event

**STATUS**: doc-only stub.  In-place extension to existing
`sched_switch.bpf.c` (no new file).  Tier-A.  **Depends on
PmuCounters landing first** (we need its perf_event_array of
CYCLES counter FDs to read from).

## Problem

Today every `TimelineSchedEvent` records `(off_cpu_ns, tid, on_cpu,
ts_ns)` — wall-clock duration, but NOT cycles.  When the CPU was
off-bench (preempted, in IRQ handler, frequency-scaled), a 100 µs
off_cpu_ns can mean wildly different things:

- 100 µs at 5 GHz = 500K cycles (we lost compute)
- 100 µs at 800 MHz idle-state = 80K cycles (we lost less compute)
- 100 µs in C6 sleep = 0 cycles (CPU was off; another tenant got
  the silicon)

Without cycle attribution per off-CPU event, we can't distinguish
"preempted to idle" from "preempted to noisy neighbor that ran our
counters down".

## Mechanism

Userspace populates a `BPF_MAP_TYPE_PERF_EVENT_ARRAY` (one slot per
CPU) with the FDs of the CYCLES counter from the PmuCounters facade
(opened in counting mode, `sample_period=0`).  In the BPF program's
switch-IN handler:

```c
struct bpf_perf_event_value cv;
long err = bpf_perf_event_read_value(&pmu_cycles_arr,
                                     BPF_F_CURRENT_CPU,
                                     &cv, sizeof(cv));
__u64 cycles_at_switch_in = cv.counter;
```

Pair with `cycles_at_switch_out` (stored in `switch_start` map
alongside the existing `ts` value) → delta = cycles ticked WHILE
this thread was off-CPU on this CPU.

## Maps

- `pmu_cycles_arr` — new `BPF_MAP_TYPE_PERF_EVENT_ARRAY`,
  max_entries = nr_cpus, populated by userspace at load-time
  with CYCLES counter FDs.
- `switch_start` — existing tid→ts map; widen value to
  `struct {ts_ns, cycles_at_switch_out}`.
- `sched_timeline` — existing; widen `TimelineSchedEvent` from
  32 B to 40 B with `__u64 off_cpu_cycles` field, OR add an
  8 B trailing extension field.

## Wire contract

`TimelineSchedEvent` adds `off_cpu_cycles` slot.  Reader can
compute `effective_freq = off_cpu_cycles / off_cpu_ns` per event
→ tells you which CPU was idle vs noisy-neighbor stealing.

Aggregate metric: `Σ off_cpu_cycles / Σ off_cpu_ns` across the
bench window = "average GHz that the CPU was running OUR off-CPU
periods at".  Augur uses this to detect frequency-scaling drift
and noisy-neighbor interference distinctly from preemption.

## Cost

`bpf_perf_event_read_value` ≈ 50-100 ns per call (kernel-side
counter readout via rdpmc-equivalent).  sched_switch already costs
us 1-2 µs/event with the existing map writes; +50-100 ns is +5-10%
on a per-event basis but absolute overhead stays ≤ 0.2% on a busy
1M-switches/sec system.

## Limits

- Requires PmuCounters' CYCLES FD per CPU.  If PmuCounters isn't
  loaded, this extension degrades to today's behavior (only
  off_cpu_ns; cycles slot left zero).  The userspace facade should
  signal "cycle attribution enabled" via a bool accessor.
- CYCLES counter under perf scheduling multiplexing: the kernel
  rotates HW counters when more events are open than HW slots.
  `bpf_perf_event_read_value` returns `enabled` and `running` so
  userspace can scale; BPF can't do FP, so we record raw counter
  delta + the {enabled, running} pair and let userspace do the
  scaling.  Widens the struct further (~+24 B per event); accept
  it.

## Implementation TODO

1. Add `pmu_cycles_arr` map declaration in `sched_switch.bpf.c`.
2. Modify switch-OUT handler to read CYCLES + store alongside ts.
3. Modify switch-IN handler to read CYCLES + compute delta.
4. Widen `timeline_sched_event` in `common.h` and the userspace
   mirror in `SchedSwitch.h` (sentinel offset asserts must update).
5. Add userspace populate-on-load: `bpf_map_update_elem(pmu_cycles_arr,
   &cpu, &cycles_fd, BPF_ANY)` for each CPU's CYCLES FD.
6. Add `SchedSwitch::attach_pmu_cycles(const PmuCounters&)` API
   for the wiring.
7. Bench harness consumes via `offcpu_max_cycles@cpu5` inline on
   the existing `└─` line.

## Sibling refs

Depends on: `PmuCounters` (for CYCLES FD).
Aggregates with: `SchedExt` (when shipped) — sched_ext can publish
these cycle deltas as scheduling-policy-input metrics.
