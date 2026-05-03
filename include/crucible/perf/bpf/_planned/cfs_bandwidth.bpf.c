/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * cfs_bandwidth.bpf.c — CFS throttle/unthrottle per cgroup; quota exhaustion.
 *
 * STATUS: doc-only stub.  Tier-D.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Containerized Crucible deployments hit CPU.cfs_quota_us limits → the
 * cgroup gets throttled (no CPU until next refill at the period
 * boundary).  Manifests as "everything went to 0 ns of compute for
 * 100ms" without any other signal.  CFS bandwidth tracepoints surface
 * the throttle/unthrottle events.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint (or fentry on cfs_throttled / unthrottled)
 * Attachment points:
 *   - tracepoint/sched/sched_cfs_throttle      — cfs_rq throttled (out)
 *   - tracepoint/sched/sched_cfs_unthrottle    — cfs_rq unthrottled (in)
 *   (or kprobe on `throttle_cfs_rq` / `unthrottle_cfs_rq` for older
 *    kernels lacking the tracepoints)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - throttle_state: HASH[cgroup_id → {throttle_ts, throttled_count}]
 * - throttle_stats: HASH[cgroup_id → {total_throttled_ns, count, max_ns}]
 * - timeline:       ARRAY[1] + BPF_F_MMAPABLE — recent {cgroup_id,
 *                   throttle_ns, period_us, quota_us, ts_ns}
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100 ns.
 * Event rate: ~1-100 throttle events/sec under tight quota; near-zero
 *   under healthy quota.  Bounded.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SchedSwitch (post-throttle our task gets switched-out).
 * Aggregates with: PsiReader (CPU pressure rises during throttle).
 * Aggregates with: iter_cgroup (read current quota + slack).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
