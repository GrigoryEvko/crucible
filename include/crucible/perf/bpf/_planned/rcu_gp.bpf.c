/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * rcu_gp.bpf.c — RCU grace period duration distribution.
 *
 * STATUS: doc-only stub (not yet implemented).  Tier-B.  See
 * _planned/INDEX.md for the menu position.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Long RCU grace periods stall every reader-side RCU callback in the
 * kernel — the callback queue grows unbounded until the GP completes.
 * On a busy system a 100ms grace period (e.g. CPU stuck in a long RCU
 * read-side critical section, or a nohz_full CPU not reporting QS) ⇒
 * milliseconds of latency added to socket close, file close, every
 * SLAB free with RCU-pending free, etc.  Currently invisible.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint (or tp_btf for ~30% lower overhead)
 * Attachment points:
 *   - tracepoint/rcu/rcu_grace_period           — GP state changes
 *   - tracepoint/rcu/rcu_quiescent_state_report — per-CPU QS reports
 *   - (optional) tracepoint/rcu/rcu_callback    — callback queued
 *   - (optional) tracepoint/rcu/rcu_invoke_callback — callback drained
 * Kernel min: 4.16 (rcu_grace_period tracepoint)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - gp_start: ARRAY[1] — current GP's start ts_ns (single in-flight GP)
 * - gp_hist:  ARRAY[N_BUCKETS] — log-scale histogram of GP durations
 *             (1µs / 10µs / 100µs / 1ms / 10ms / 100ms / 1s)
 * - gp_max:   ARRAY[1] — running worst-case GP duration
 * - gp_count: ARRAY[1] — total grace periods completed
 * - cpu_qs_lag: PERCPU_ARRAY[1] — per-CPU last GP-to-QS-report lag
 *               (high lag = nohz_full CPU not pinging the GP machinery)
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — circular buffer of recent
 *             {start_ts, end_ts, duration, gp_seq, n_qs_reports}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_rcu_gp_event {
 *     __u64 start_ts;
 *     __u64 duration_ns;
 *     __u64 gp_seq;       // monotonically increasing
 *     __u32 n_qs_reports; // how many CPUs reported QS for this GP
 *     __u32 _pad;
 *     __u64 ts_ns;        // WRITTEN LAST — completion marker
 *     __u64 _pad8;        // cache-line coresidence
 * };  // 40 B → 48 B with pad (64/48 != integer; bump to 64 B per slot)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns (one map lookup + one atomic add + one
 * histogram bucket store).
 * Expected event rate: ~10-100 GPs/sec on a healthy system; up to
 * 10K/sec on heavy SLAB churn.
 * Per-sec overhead: ≤0.001% normal; up to 0.01% under churn.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Multi-flavor RCU (rcu_sched, rcu_bh, rcu_preempt) folded into
 *   one stream by name on modern kernels (single-flavor since 4.20).
 * - Expedited grace periods (rcu_exp_*) are a separate path; opt-in
 *   tracepoint set if needed.
 * - Per-CPU QS lag requires walking nr_cpus on read which means
 *   userspace iterates the PERCPU_ARRAY at bench-end (cheap).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SchedSwitch (long GPs correlate with off-CPU
 *   stalls in callback-draining tasks).
 * Replaces: nothing.
 * Conflicts: none.
 */

#include "../common.h"

/* TODO: implement.  Mirror sched_switch.bpf.c structure: maps,
 * tracepoint handlers with ts_ns LAST + compiler barrier, userspace
 * facade reading the histogram + timeline. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
