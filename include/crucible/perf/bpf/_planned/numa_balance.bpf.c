/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * numa_balance.bpf.c — per-decision NUMA balancing attribution.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-2 addition.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SenseHub aggregates `MM_NUMA_PAGE_MIGRATE_*` counts.  We don't see
 * WHICH page migrations happened, WHY (private/shared fault, scan-pte),
 * or which task was the migration target.  Per-decision attribution
 * lets Augur diagnose "this task is bouncing between sockets — pin it"
 * vs "this is healthy lazy migration".
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified against include/trace/events/sched.h on
 * kernel 6.17):
 *   - tracepoint/sched/sched_swap_numa          — bidirectional swap
 *                                                  picked (src↔dst tasks)
 *   - tracepoint/sched/sched_stick_numa         — task pinned to current
 *   - tracepoint/sched/sched_move_numa          — one-way move
 *   - tracepoint/migrate/mm_migrate_pages       — per-bulk migration result
 *   - tracepoint/migrate/mm_migrate_pages_start — bulk start timing
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_task: LRU_HASH[tid → {migrations, bytes_migrated, last_node,
 *                              flap_count}] — track per-task bouncing
 * - per_reason: HASH[reason → count] — why_failed buckets
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: 10-1000/sec on healthy NUMA host.
 * Per-sec overhead: <0.001%.  Default-on safe.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Per-task counts can grow unboundedly; LRU eviction at 4096 tasks.
 * - autonuma scan rate (/proc/sys/kernel/numa_balancing_scan_*)
 *   affects event rate; document expected baseline at facade load.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SenseHub `MM_NUMA_*` (counters), iter_task.bpf.c
 *   (per-thread RSS / cgroup attribution at snapshot time).
 * Inputs to: Augur task-pinning recommendations.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
