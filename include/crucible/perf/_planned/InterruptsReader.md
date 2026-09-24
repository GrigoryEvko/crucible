# `InterruptsReader` — `/proc/interrupts` per-CPU per-source IRQ counts

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/InterruptsReader.h`.  Userspace-only.

## Problem

`hardirq.bpf.c` (planned) gives per-IRQ entry/exit timestamps via
BPF — high resolution, ~50 ns overhead per IRQ.  For aggregate
"how many IRQs per CPU per source" answers, `/proc/interrupts`
is much cheaper: zero kernel-side cost (counters maintained
unconditionally by the kernel), tens-of-microseconds polling.

Verified format on 6.17:
```
            CPU0       CPU1       CPU2  ...  CPUN
   0:        136          0          0          0    IO-APIC    2-edge      timer
   7:          0          0          0          0    IO-APIC    7-fasteoi
  ...
LOC:    1234567   1234560    ...                  Local timer interrupts
RES:     232100     230900    ...                  Rescheduling interrupts
TLB:      45123      44987    ...                  TLB shootdowns
```

Per-row: vector ID + per-CPU count + controller + name.  Special
rows at end (LOC/RES/TLB/CAL/...) cover internal kernel IPIs.

## Mechanism

Polled at 1 Hz (cold).  Per-poll: ~50 µs (parse 100-200 lines).
Sample takes per-CPU per-source delta.

## Wire contract

```cpp
struct InterruptsRow {
    uint32_t vector_id;          // numeric or special (LOC/RES/TLB/...)
    char     name[32];
    char     controller[16];     // IO-APIC / PCI-MSI / ...
    std::span<const uint64_t> per_cpu_count;
};
class InterruptsReader {
    std::span<const InterruptsRow> snapshot();
    // Aggregate by source category (timer / disk / nic / ipi):
    uint64_t total_for_category(IrqCategory);
};
```

## Bench harness display

```
└─ irqs: nic0=2.3M/s (high; check ring config)  timer=4K/s (LOC)
         tlb=120/s (TLB shootdowns)  ipi_resched=350/s
```

## Cost model

- Per-poll: ~50 µs (file read + parse).
- 1 Hz cadence: ~0.005% CPU.  Effectively free.

## Known limits

- Aggregate by-CPU; doesn't attribute to specific tasks.  Use
  `hardirq.bpf.c` for per-IRQ timing.
- Internal IPIs (LOC/RES/TLB/CAL) are at the end with non-numeric
  "vector"; parser treats specially.
- `/proc/interrupts` row count grows with device count; large hosts
  have 50-200 rows, parser scales O(rows × cpus).

## Sibling refs

- **Aggregate-counter complement** to: `hardirq.bpf.c` (per-event timing).
- **IPI breakdown**: TLB row complements `ipi_events.bpf.c`; RES row
  pairs with `irq_vectors.bpf.c` reschedule_entry counts.
- **NIC ring config validator**: high single-NIC IRQ rate suggests
  NIC RSS misconfig — validates against `cog/NicConfig.h` (planned).
