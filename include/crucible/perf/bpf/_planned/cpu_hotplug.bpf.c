/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * cpu_hotplug.bpf.c — CPU online/offline lifecycle observation.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-2 addition.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Elastic deployments (cloud spot, k8s autoscaling, datacenter
 * power-capping) bring CPUs in and out at runtime.  Crucible benches
 * pinning to CPU N silently break when N goes offline; affinity masks
 * silently shrink; per-CPU caches lose entries; load balancing
 * reshuffles.  All currently invisible to the bench harness.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/cpuhp/cpuhp_enter         — state-machine step
 *   - tracepoint/cpuhp/cpuhp_exit          — state-machine step done
 *   - tracepoint/cpuhp/cpuhp_multi_enter   — multi-CPU op
 *   - tracepoint/power/cpu_idle (subset)   — entry to/exit from offline
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - hotplug_events: ARRAY[1] + BPF_F_MMAPABLE — every cpuhp transition
 * - cpu_state: ARRAY[NR_CPUS] — current state per CPU
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: 0/sec on stable host; bursts of
 * ~30 events per offline/online (state machine has ~30 steps).
 * Effectively free always-on.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Hotplug state-machine steps are kernel-version dependent
 *   (cpuhp_state enum changes).  Userspace decoder needs per-kernel
 *   table.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SchedSwitch (existing — per-task scheduling) — when CPU
 *   hotplugs out, all tasks pinned to it get migrated; SchedSwitch
 *   sees the migration but not the WHY.
 * Sibling: cpu_idle.bpf.c (planned) — C-state observation.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
