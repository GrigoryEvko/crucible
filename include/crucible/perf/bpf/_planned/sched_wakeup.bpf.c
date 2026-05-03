/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sched_wakeup.bpf.c — Wakeup latency timeline (sched_waking → sched_switch).
 *
 * STATUS: doc-only stub.  Tier-D.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SchedSwitch tells us off-CPU duration.  sched_wakeup tells us
 * "scheduler decided to run this task" — the gap from sched_waking
 * (signal sent) to sched_switch in (actually got CPU) is wakeup
 * latency, the most direct measure of "how long after the kernel
 * decided we should run did we actually run".
 *
 * Useful for diagnosing: contention with higher-priority task,
 * cross-CPU migration cost on wakeup, NUMA-remote wakeup penalty.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/sched/sched_waking      — wakeup signal sent
 *   - tracepoint/sched/sched_wakeup_new  — wakeup of new task
 *   - tracepoint/sched/sched_switch      — actually switched in
 *     (correlate to compute latency)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - wake_pending:  HASH[(target_tid) → (wake_ts, waker_tid, target_cpu)]
 *                  LRU_HASH, max 16384
 * - wake_lat_hist: HASH[(target_tid) → log-bucket histogram]
 *                  per-thread wakeup latency distribution
 * - waker_pairs:   HASH[(waker_tid, target_tid) → count]
 *                  who wakes whom (top-K wakee-waker pairs)
 * - timeline:      ARRAY[1] + BPF_F_MMAPABLE — recent {target_tid,
 *                  wake_ts, run_ts, wake_lat_ns, target_cpu, on_cpu}
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100 ns (waking + matching switch_in lookup).
 * Event rate: 10K-1M wakeups/sec on multi-thread.
 * Per-sec overhead: 0.1-1%.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SchedSwitch (off-CPU duration) — wakeup latency is the
 *   "from-runnable" subset of off-CPU.
 * Aggregates with: SenseHub `WAKEUPS_RECEIVED` / `WAKEUPS_SENT`
 *   (aggregate counts).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
