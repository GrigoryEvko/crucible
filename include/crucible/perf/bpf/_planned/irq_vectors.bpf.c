/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * irq_vectors.bpf.c — x86 per-APIC-vector IRQ entry/exit attribution.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  x86-only
 * (verified against /sys/kernel/tracing/events/irq_vectors/ on 6.17 —
 * 36 events).  ARM equivalent uses arch/arm64-specific tracepoints.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * `irq/irq_handler_entry` (existing `hardirq.bpf.c` plan) gives us
 * generic IRQ handler attribution by device IRQ number.  But on x86,
 * the FULL picture lives one level deeper — the APIC vector level,
 * exposing kernel-internal IPI categories that don't show up as
 * device IRQs:
 *
 *   - reschedule_entry/exit       — wake-up IPI (most frequent on busy hosts)
 *   - call_function_entry/exit    — smp_call_function_many dispatch
 *   - call_function_single_entry/exit — smp_call_function_single
 *   - local_timer_entry/exit      — periodic timer tick
 *   - irq_work_entry/exit         — deferred work from IRQ context
 *   - thermal_apic_entry/exit     — thermal threshold trip
 *   - threshold_apic_entry/exit   — MCE threshold (ECC near-full)
 *   - error_apic_entry/exit       — APIC delivery error (BAD)
 *   - spurious_apic_entry/exit    — phantom IRQ (BAD)
 *   - deferred_error_apic         — AMD MCE deferred error
 *   - x86_platform_ipi            — platform-specific IPI dispatch
 *
 * Plus vector-allocation lifecycle events:
 *   - vector_alloc / vector_alloc_managed
 *   - vector_clear / vector_free_moved
 *   - vector_setup / vector_teardown
 *   - vector_reserve / vector_reserve_managed
 *   - vector_activate / vector_deactivate
 *   - vector_config / vector_update
 *
 * For latency-critical bench, "10 µs steal" attributed to "IRQ" is
 * vacuous.  "10 µs steal from `local_timer_entry` × 2 + `reschedule`
 * × 1" is actionable (turn off the periodic profiler that's firing
 * the timer reschedules).
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points: subset of the 36 irq_vectors events above.
 * Default subscribed set:
 *   - reschedule_entry/exit
 *   - call_function_entry/exit
 *   - call_function_single_entry/exit
 *   - local_timer_entry/exit
 *   - irq_work_entry/exit
 *   - thermal_apic_entry/exit       (rare; load-bearing when fires)
 *   - threshold_apic_entry/exit     (rare; load-bearing)
 *   - error_apic_entry/exit         (rare; load-bearing)
 *   - spurious_apic_entry/exit      (rare; load-bearing)
 *
 * Vector-lifecycle events (alloc/clear/setup/etc) NOT subscribed by
 * default — high startup-time rate, low steady-state value.  Opt-in
 * via `CRUCIBLE_PERF_IRQ_VECTOR_LIFECYCLE=1`.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - inflight: PERCPU_ARRAY[N_VECTORS] — per-vector entry timestamp
 *             (one slot per default-subscribed vector kind)
 * - per_vector: HASH[vector_kind → {count, total_ns, max_ns}]
 *               vector_kind enum: RESCHEDULE / CALL_FN / CALL_FN_SINGLE /
 *                                  LOCAL_TIMER / IRQ_WORK / THERMAL /
 *                                  THRESHOLD / ERROR / SPURIOUS / X86_PLATFORM
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — recent {cpu, vector_kind,
 *             handler_ns, ts_ns}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_irq_vector_event {
 *     uint64_t handler_ns;           // entry → exit
 *     uint16_t cpu;
 *     uint8_t  vector_kind;          // VectorKind enum
 *     uint8_t  _pad[5];
 *     uint64_t ts_ns;                // WRITTEN LAST
 * };  // 24 B (padded to 32 for cache-line coresidence)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns × 2 tracepoints (entry + exit) = ~100 ns/IRQ.
 * Event rate: 1K-100K/sec on busy host (reschedule_entry dominates).
 * Per-sec overhead: 0.01-1%.
 *
 * Default-off; opt-in via `CRUCIBLE_PERF_IRQ_VECTORS=1` for tail-
 * latency analysis.  Sub-set via `CRUCIBLE_PERF_IRQ_VECTOR_KINDS=
 * "reschedule,call_function,local_timer"` to limit overhead.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - x86-only.  ARM has different vector taxonomy (SGI numbers, SMC
 *   handler dispatch); separate `arm_irq_vectors.bpf.c` future facade.
 * - Spurious_apic / error_apic firing >0/sec ALWAYS indicates hardware
 *   trouble; bench harness should banner-warn.
 * - Vector enumeration in kernel can shift across versions; per-kernel
 *   table at userspace decode time.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Refines: hardirq.bpf.c (generic device-IRQ entry/exit; this is the
 *   per-vector breakdown of what the generic facade groups into "IRQ").
 * Sibling: ipi_events.bpf.c (raw `ipi/ipi_send_*` send-side; this is
 *   the receive-side per-vector handler latency).
 * Sibling: csd.bpf.c (the smp_call_function_* sub-case of
 *   `call_function_entry/exit`).
 * Aggregates with: SchedSwitch (long IRQ handler → preemption window).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
