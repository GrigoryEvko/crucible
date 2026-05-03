/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * iter_cgroup.bpf.c — Walk cgroup hierarchy.
 *
 * STATUS: doc-only stub.  Tier-G.  BPF iter ITER_TYPE_CGROUP
 * (kernel 5.13+).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Per-cgroup resource limits + current usage: cpu.max (quota +
 * period), memory.max + memory.current, io.max, plus the children
 * tree.  Useful for "are we close to the cgroup CPU/memory limit"
 * proactive throttle prediction.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: BPF_TRACE_ITER ITER_TYPE_CGROUP
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct cgroup_iter_record {
 *     __u64 cgroup_id;
 *     __u64 parent_id;
 *     __u32 level;             // depth in hierarchy
 *     __u32 _pad;
 *     __u64 cpu_quota_us, cpu_period_us, cpu_used_us;
 *     __u64 mem_max_bytes, mem_current_bytes;
 *     __u64 io_max_rbps, io_max_wbps, io_current_rbps;
 *     char  name[64];
 * };
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-cgroup: ~300 ns.  Modern systems have 100-1000 cgroups →
 *   ~30-300 µs total.  Run periodically (10 Hz default).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: CfsBandwidth (CFS throttle events; this is current state).
 * Sibling: PsiReader (per-cgroup PSI also available via /sys).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
