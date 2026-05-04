/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * preemptirq.bpf.c — preempt-disable / IRQ-disable section duration tracking.
 *
 * STATUS: doc-only stub.  Tier-1 audit-round-2 addition.  Underlies
 * any "what's the longest stretch our task ran without being
 * preemptible" question.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * The kernel disables preemption (cannot be context-switched out)
 * and IRQs (cannot be interrupted) for various critical sections:
 * - spinlock acquire (preempt-disable)
 * - local_irq_disable for critical sections
 * - per-cpu data access via get_cpu_var
 * - softirq processing
 *
 * These create LATENCY HOLES — periods where our task can't be
 * preempted-onto a CPU even when scheduler wants to.  For latency-
 * critical bench code the longest preempt-disable section in the
 * kernel directly bounds our worst-case wait.
 *
 * preemptirq tracepoints expose the exact moments preempt/IRQ are
 * disabled and re-enabled, with caller IP.  Per-section duration
 * histograms let us identify "the kernel held preempt-disable for
 * 80 µs in __schedule()" type insights.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (require CONFIG_PREEMPTIRQ_TRACEPOINTS=y):
 *   - tracepoint/preemptirq/preempt_disable
 *   - tracepoint/preemptirq/preempt_enable
 *   - tracepoint/preemptirq/irq_disable
 *   - tracepoint/preemptirq/irq_enable
 *
 * Each carries `caller_addr` + `parent_addr` for IP-attribution.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - inflight: PERCPU_ARRAY[2] — slot[0]=preempt_disable_ts,
 *                                slot[1]=irq_disable_ts (per CPU)
 * - per_caller_lat: LRU_HASH[caller_addr → log-bucket histogram]
 *                   per-IP duration distribution; userspace resolves
 *                   caller_addr via /proc/kallsyms
 * - max_hold: PERCPU_ARRAY[2] — running max preempt-/IRQ-disable
 *                                period observed this CPU
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — recent {cpu, kind,
 *             duration_ns, caller_addr, ts_ns}
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns × 2 (disable + enable pair) = ~100 ns.
 * Event rate: HIGH — every spinlock_lock fires, every page-fault
 * fires, every syscall touches preempt-disable.  10M-100M events/sec
 * on busy host.  Per-sec overhead: 1-10% — TOO HIGH for always-on.
 *
 * Default off; opt-in via `CRUCIBLE_PERF_PREEMPTIRQ=1` for
 * latency-diagnosis runs.  Sample-period gating recommended.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - CONFIG_PREEMPTIRQ_TRACEPOINTS often disabled in distro kernels
 *   (default-on in kernel 5.15+ but distros sometimes turn off).
 *   Load() returns nullopt with diagnostic if missing.
 * - Per-CPU bookkeeping; nested disables (IRQ disable within
 *   preempt-disable) handled via separate slots.
 * - Caller IP resolution requires kallsyms read at userspace; cache.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: HardIrq (per-IRQ duration; this is preempt+IRQ section
 *   duration, aggregate over all reasons)
 * Aggregates with: SchedSwitch (long preempt-disable → delayed
 *   wakeup → off-CPU spike on the wakee task)
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
