/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * ipi_events.bpf.c — full IPI (Inter-Processor Interrupt) traffic.
 *
 * STATUS: doc-only stub.  Tier-1 audit-round-2 addition.  Superset
 * of SenseHub-extension's `TLB_SHOOTDOWNS` counter.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Cross-CPU coordination cost.  IPI categories on x86_64:
 *   - TLB shootdown IPI (covered partially in SenseHub ext)
 *   - Reschedule IPI (waking task on remote CPU)
 *   - Function-call IPI (smp_call_function on N CPUs)
 *   - NMI IPI (perf_event sampling fires this on remote CPU)
 *   - CPU stop IPI (cpu_stop_*; rare)
 *   - Single-step / KGDB IPI (debug)
 *   - Reboot IPI (panic)
 *   - Thermal IPI (throttle propagation)
 *
 * Each IPI is ~1-5 µs on x86 (LAPIC IPI delivery + handler).  On a
 * busy multi-core system, IPI cost can be 5-15% of CPU.  SenseHub
 * extension covers only the TLB-shootdown subset.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint + arch-specific kprobe
 * Attachment points:
 *   - tracepoint/ipi/ipi_send_cpu      — IPI dispatched to one CPU       [REAL, all-arch]
 *   - tracepoint/ipi/ipi_send_cpumask  — IPI dispatched to N CPUs        [REAL, all-arch]
 *   - tracepoint/ipi/ipi_raise         — categorical raise               [ARM64-only — only arm64/kernel/smp.c calls trace_ipi_raise()]
 *   - tracepoint/ipi/ipi_entry         — handler entered                 [ARM64-only]
 *   - tracepoint/ipi/ipi_exit          — handler returned                [ARM64-only]
 *
 * x86 has no IPI handler entry/exit tracepoints; observation requires
 * raw_tracepoint on irq_vectors (call_function_entry, call_function_single_entry,
 * reschedule_entry, etc. — see /sys/kernel/tracing/events/irq_vectors/) for the
 * x86-specific equivalent.  Per-arch implementation required.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_reason: HASH[reason_string → {count, total_handler_ns,
 *                                      max_handler_ns}]
 *               reason: "TLB", "RESCHEDULE", "CALL_FUNCTION",
 *                       "NMI", "CPU_STOP", "REBOOT", "THERMAL"
 * - per_pair:  LRU_HASH[(src_cpu, dst_cpu) → ipi_count]
 *              cross-socket pairs are most expensive
 * - inflight:  PERCPU_ARRAY[1] — per-CPU IPI handler entry ts
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * No timeline (rate too high — could be 1M IPIs/sec on busy host).
 * Histogram + counter snapshot per bench window.
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns × 2 tracepoints (entry + exit) = ~150 ns.
 * Event rate: 10K-1M IPIs/sec on busy hosts.
 * Per-sec overhead: 0.15-15% — high.  Default-OFF; opt-in via env
 * var `CRUCIBLE_PERF_IPI=1` for diagnosis runs.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - x86-only handler classification; ARM has different IPI categories
 *   (`SGI` numbers); per-arch reason table.
 * - Cross-socket IPIs (~10× more expensive than intra-socket) need
 *   topology lookup at userspace to weight properly.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Subsumes: SenseHub-ext `TLB_SHOOTDOWNS` (which is just one IPI
 *   reason).  When this facade lands, can demote the SenseHub
 *   counter to a derived view.
 * Aggregates with: HardIrq (hardirq_handler is what runs on the
 *   destination CPU), SchedSwitch (rescheduling IPI → context switch).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
