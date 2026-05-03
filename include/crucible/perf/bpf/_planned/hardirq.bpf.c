/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * hardirq.bpf.c — Per-IRQ handler entry→exit duration timeline.
 *
 * STATUS: doc-only stub.  Tier-B.  Sibling to `softirq` already
 * tracked aggregate-only in SenseHub (`SOFTIRQ_STOLEN_NS`).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * "max=460ns when median=4ns" → an IRQ stole 100µs.  WHICH device?
 * SenseHub aggregates IRQ time but not per-vector; we can't tell
 * "NIC RX storm" from "NVMe completion burst" from "thermal interrupt".
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/irq/irq_handler_entry — IRQ N entered handler
 *   - tracepoint/irq/irq_handler_exit  — IRQ N returned (carries `ret`)
 * Kernel min: 2.6.28 (universal).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - irq_start: PERCPU_ARRAY[NR_IRQS=256] — per-CPU per-IRQ start ts
 *              (an IRQ can interrupt itself across CPUs but not on
 *              the same CPU — per-CPU is sufficient)
 * - irq_stats: HASH[irq_nr → {count, total_ns, max_ns, irq_name[32]}]
 *              irq_name resolved via /proc/interrupts at userspace read
 * - timeline:  ARRAY[1] + BPF_F_MMAPABLE — recent {cpu, irq_nr,
 *              duration_ns, ts_ns} for outlier diagnosis
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_hardirq_event {
 *     __u64 duration_ns;
 *     __u32 irq_nr;
 *     __u32 cpu;
 *     __u64 ts_ns;          // WRITTEN LAST
 *     __u64 _pad8;
 * };  // 32 B (cache-line coresident)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns (one PERCPU_ARRAY lookup at entry, same +
 * one HASH update at exit).
 * Event rate: 1K-100K IRQs/sec depending on NIC/NVMe load.  Top-tier
 * hosts can hit 1M IRQs/sec under network storm — gate behind a
 * sample-period (record every Nth IRQ) if measured cost > 0.5%.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - irq_name resolution requires reading /proc/interrupts at
 *   userspace setup; cache the mapping.  Re-read on rebalance
 *   (rare) is acceptable.
 * - No CPU affinity-vs-actual mismatch detection — would require
 *   reading `/proc/irq/N/smp_affinity` at userspace.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SenseHub `SOFTIRQ_STOLEN_NS` (softirq aggregate; we have).
 * Aggregates with: SchedSwitch (preempt during long IRQ).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
