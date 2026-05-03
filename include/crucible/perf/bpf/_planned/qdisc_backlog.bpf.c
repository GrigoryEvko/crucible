/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * qdisc_backlog.bpf.c — Per-qdisc enqueue/dequeue backlog depth.
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Qdisc backlog growth predicts TX-side latency: if our outbound
 * qdisc (fq, mq, prio) is consistently 100+ packets deep, every new
 * send waits behind those.  CNTP needs sub-µs send latency; backlog
 * > 16 packets is already a warning.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/qdisc/qdisc_enqueue
 *   - tracepoint/qdisc/qdisc_dequeue
 * (Older kernels may need kprobe on `qdisc_enqueue` / `qdisc_dequeue`.)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_qdisc_depth: HASH[qdisc_handle → {current_depth, max_depth,
 *                                          drops, enqueue_count, dequeue_count}]
 * - depth_hist: ARRAY[16] — log-bucket depth distribution
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-enqueue/dequeue: ~50 ns.  Event rate: line-rate.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: TcEgressStats (per-class accounting; this adds depth).
 * Aggregates with: SockOpsObserver (high backlog → BBR/CUBIC pacing
 *   adjustments visible in sock_ops).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
