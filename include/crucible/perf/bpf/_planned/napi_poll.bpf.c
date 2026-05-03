/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * napi_poll.bpf.c — napi/napi_poll driver poll cycles per NAPI instance.
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * NAPI is the kernel's interrupt-coalescing mechanism for high-rate
 * NICs.  How often does the driver poll? How many frames per poll?
 * Did it hit the budget (i.e., ran out of CPU before draining the
 * ring)? — diagnoses RPS imbalance, IRQ moderation tuning, NIC ring
 * underrun.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/napi/napi_poll          — poll completed (work, budget)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_napi: HASH[(napi_id) → {polls, work_total, budget_hit_count,
 *                                avg_work_per_poll}]
 * - busy_polls: ARRAY[1] — polls that drained budget (likely NIC
 *               saturated)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-poll: ~50 ns.  Polls fire ~1K-100K/sec/NIC.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: NetifReceive (post-poll arrival).
 * Sibling: HardIrq (NIC IRQ → NAPI schedule).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
