/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sched_rq.bpf.c — Run-queue depth via fentry on enqueue/dequeue_task_fair.
 *
 * STATUS: doc-only stub.  Tier-D.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * "How busy is this CPU?" answered as run-queue length over time.
 * Per-CPU depth distribution → tells you which CPU is oversubscribed
 * (depth ≫ 1 means our task waits behind others) vs well-balanced
 * (depth ≈ 1 means we mostly own the CPU).  Augur's input for
 * cpu-sticky placement decisions.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: fentry (much cheaper than kprobe; ~50 ns vs ~1 µs)
 * Attachment points:
 *   - fentry/enqueue_task_fair  — task added to CFS run queue
 *   - fexit/enqueue_task_fair   — get rq->cfs.h_nr_running post-enqueue
 *   - fentry/dequeue_task_fair  — task removed
 *   - fexit/dequeue_task_fair   — get rq->cfs.h_nr_running post-dequeue
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - rq_depth_hist: PERCPU_ARRAY[N_BUCKETS=16] — log-bucketed depth
 *                  distribution per CPU (0, 1, 2-3, 4-7, 8-15, ...)
 * - rq_depth_max:  PERCPU_ARRAY[1] — running max depth per CPU
 * - rq_depth_now:  PERCPU_ARRAY[1] — current depth (live snapshot)
 * - timeline:      ARRAY[1] + BPF_F_MMAPABLE — sampled {cpu, depth, ts_ns}
 *                  every Nth enqueue/dequeue (gate to bound rate)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-80 ns (fentry + map update).
 * Event rate: ~enqueue/dequeue rate ≈ ~2× context-switch rate
 *   (1K-10M/sec on busy hosts).
 * Per-sec overhead: 0.05-0.5%; gate timeline emission to 1-in-1024
 *   sample period to bound mmap pressure.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - fentry/fexit on `enqueue_task_fair` requires kernel symbol presence
 *   (always present in CFS); falls back to kprobe on RT/idle classes.
 * - sched_ext-managed tasks bypass enqueue_task_fair; if sched_ext
 *   is loaded, this program misses sched_ext-controlled enqueues.
 *   Document the interaction.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: sched_wakeup (wakeup latency proportional to rq depth).
 * Aggregates with: SchedExt (sched_ext sees the same depth).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
