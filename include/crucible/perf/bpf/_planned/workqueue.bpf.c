/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * workqueue.bpf.c — Per-workqueue depth + execute_start→execute_end latency.
 *
 * STATUS: doc-only stub.  Tier-B.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Kernel workqueues run deferred work in kernel threads (kworker/*).
 * If a wq backs up — async writeback, IO completion, network softirq
 * deferral, GPU driver tail work — our compute thread shares CPUs
 * with the catching-up kworkers, getting preempted in unpredictable
 * patterns.  SchedSwitch tells us "we got preempted"; this tells us
 * "by which workqueue, doing how much work".
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/workqueue/workqueue_queue_work    — work item queued
 *   - tracepoint/workqueue/workqueue_activate_work — work actually scheduled
 *   - tracepoint/workqueue/workqueue_execute_start — handler entered
 *   - tracepoint/workqueue/workqueue_execute_end   — handler returned
 * Kernel min: 4.0 (all four tracepoints).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - wq_inflight: HASH[(work_struct *) → start_ts]   — per-work execution start
 *                LRU_HASH, max_entries 16384 — long-tail kworkers
 * - wq_backlog:  HASH[(workqueue_struct *) → {queue_count, drain_count}]
 *                — queue-vs-drain delta = current backlog
 * - wq_lat_hist: HASH[(workqueue_struct *) → log-bucket histogram]
 *                — per-wq execute latency distribution
 * - timeline:    ARRAY[1] + BPF_F_MMAPABLE — recent {wq_name, work_func,
 *                queue_ts, start_ts, end_ts, cpu}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_workqueue_event {
 *     __u64 work_func;       // function pointer (kallsyms-resolvable)
 *     __u64 queue_to_run_ns; // queue→start (queue latency)
 *     __u64 run_ns;          // start→end (execute latency)
 *     __u32 cpu;             // which CPU ran it
 *     __u32 wq_id;           // hash of workqueue_struct addr
 *     __u64 ts_ns;           // WRITTEN LAST
 * };  // 40 B; pad to 48 or 64 per cache-line policy
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100-200 ns (4 tracepoints × 50 ns + map churn).
 * Expected event rate: 1K-100K work items/sec on a busy host.
 * Per-sec overhead: ~0.01-1% — gate the per-event map writes by
 * `is_target() || is_kworker_for_our_io()` if needed (kworker doesn't
 * run on our_tids; needs different filter — ALL kworkers system-wide).
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - System-wide tracking; no PID filter (kworkers don't carry our
 *   tgid).  Documented; consider a wq-name allowlist (e.g.
 *   "events", "writeback", "kblockd_*") to bound rate.
 * - Per-CPU workqueues vs unbound workqueues — both captured; the
 *   `cpu` field carries `bpf_get_smp_processor_id()` at execute_start.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SchedSwitch (kworker_exec → preempt of our task on
 *   the same CPU).
 * Promoted from: SenseHub (would be too high-rate as a single counter).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
