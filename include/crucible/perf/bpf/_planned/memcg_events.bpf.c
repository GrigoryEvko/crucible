/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * memcg_events.bpf.c — per-cgroup memory.high / memory.max threshold events.
 *
 * STATUS: doc-only stub.  Tier-1 audit-round-2 addition.  Per-cgroup
 * OOM precursors before SenseHub's `OOM_KILLS_SYSTEM` fires.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SenseHub `OOM_KILLS_SYSTEM` catches the kill (after-the-fact).
 * Memcg events fire BEFORE the kill — when a cgroup hits its
 * `memory.high` (soft throttle), `memory.max` (hard limit), or
 * `memory.oom_control` thresholds.  These are early-warning signals:
 * "we're approaching the cgroup's memory ceiling — back off or
 * spill to disk before the OOM-killer fires".
 *
 * For containerized Crucible deployments (k8s, docker), this is the
 * difference between graceful degradation and a SIGKILL'd worker.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (kernel 5.x — cgroup-v2 memcg).
 * Verified against /sys/kernel/tracing/events/ on kernel 6.17:
 *   - tracepoint/ras/memory_failure_event            — hardware memory error
 *                                                       (NOT memory_failure/* —
 *                                                        no such subsystem)
 *   - tracepoint/oom/mark_victim                     — already in SenseHub
 *   - tracepoint/oom/wake_reaper                     — OOM reaper kicked off
 *   - tracepoint/oom/start_task_reaping
 *   - tracepoint/oom/finish_task_reaping
 *   - tracepoint/oom/skip_task_reaping
 *   - tracepoint/memcg/count_memcg_events            — bonus: per-cgroup event tally
 *   - tracepoint/memcg/memcg_flush_stats             — accounting flush hook
 *   - tracepoint/memcg/mod_memcg_state               — per-cgroup counter modify
 *
 * Plus userspace-side polls of `/sys/fs/cgroup/<path>/memory.events`
 * which exposes cumulative counts of:
 *   - low (memory.low breached)
 *   - high (memory.high breached → throttle)
 *   - max (memory.max breached → would-OOM)
 *   - oom (OOM kill triggered)
 *   - oom_kill (number of OOM kills in this cgroup)
 *   - oom_group_kill (whole cgroup killed atomically)
 *
 * The /proc poll path is simpler than tracepoints for the threshold
 * events; this facade can fold both — tracepoints for "WHEN exactly"
 * and /proc for cumulative "HOW OFTEN".
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_cgroup: HASH[cgroup_id → {low_count, high_count, max_count,
 *                                  oom_count, oom_kill_count}]
 *               LRU_HASH; eviction on cgroup remove
 * - timeline:   ARRAY[1] + BPF_F_MMAPABLE — recent {cgroup_id,
 *               threshold_kind, ts_ns}
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: typically 0/sec on healthy
 * deployment; could fire 10-100/sec under sustained pressure.
 * Effectively free.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - cgroup-v2 only (cgroup-v1 has separate APIs we don't bother with).
 * - threshold tracepoints are coarse (one event when the threshold
 *   is breached); fine-grained "approaching" detection requires
 *   polling memory.current vs memory.high differential.
 * - Per-cgroup paths require translation via `iter_cgroup.bpf.c`
 *   (sibling) → cgroup_id → cgroup_path mapping for human-readable
 *   bench output.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SenseHub `OOM_KILLS_SYSTEM` / `OOM_KILL_US` (after-the-fact).
 * Sibling: PsiReader (cgroup memory.pressure for graduated readout).
 * Sibling: iter_cgroup (cgroup_id → path resolution).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
