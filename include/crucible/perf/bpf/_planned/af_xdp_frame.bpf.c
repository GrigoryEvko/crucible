/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * af_xdp_frame.bpf.c — AF_XDP socket frame ingress/egress accounting (CNTP).
 *
 * STATUS: doc-only stub.  Tier-F.  CNTP fast-path observability.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * CNTP rides AF_XDP frames between Relays.  Frame queue depths,
 * UMEM ring fill rates, completion-ring drain rates all directly
 * predict cross-Relay throughput.  Userspace AF_XDP observability is
 * blind to "kernel-side queue is filling because driver is slow" vs
 * "userspace is slow draining the rx ring".
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint + xdp companion
 * Attachment points:
 *   - tracepoint/xdp/xdp_redirect      — XDP redirected to AF_XDP
 *   - tracepoint/xdp/xdp_redirect_err  — redirect failed (slot full)
 *   - tracepoint/xdp/mem_disconnect    — UMEM detached (cleanup)
 *   - kprobe/xsk_rcv                   — frame received into xsk
 *   - kprobe/xsk_generic_rcv           — generic-mode reception
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_xsk: HASH[(ifindex, queue_id) → {rx_packets, rx_dropped,
 *            tx_packets, tx_completions, fill_q_depth, comp_q_depth}]
 * - drop_reason: ARRAY[N_REASONS] — XDP_REDIRECT failure causes
 *                (XSKMAP slot empty, ring full, etc.)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~30-50 ns (tracepoint), ~80-150 ns (kprobe).
 * Event rate: line-rate.  At 14 Mpps the kprobe path is too costly;
 *   tracepoint-only mode for production, full kprobe for diagnosis.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: XdpRxStats (XDP_REDIRECT to AF_XDP destination); this
 *   tracks the destination-side delivery.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
