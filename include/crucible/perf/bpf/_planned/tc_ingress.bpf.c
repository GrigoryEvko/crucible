/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * tc_ingress.bpf.c — tc-bpf ingress: post-XDP frame attribution + skb metadata.
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Frames that XDP_PASS'd (didn't go to AF_XDP) hit tc-bpf ingress
 * before going up the kernel stack.  This is where to attribute
 * federation TCP flows (mTLS / QUIC) — XDP only sees raw IP, by
 * tc-ingress the kernel has built the skb with metadata.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: sched_cls (tc-bpf, ingress side)
 * Attach: `tc filter add dev ethN ingress bpf direct-action`
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_class: ARRAY[N_CLASSES=8] — same shape as tc_egress
 * - per_src_ip: LRU_HASH[src_ipv6 → bytes] — top-K ingress sources
 *               (federation peer attribution)
 * - flow_table: LRU_HASH[5tuple → flow_stats] — for matching to
 *               outgoing CNTP RTT measurements
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-packet: ~30-50 ns.  Per-NIC ingress.
 * Event rate: line-rate (modulo XDP pre-filtering).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: XdpRxStats (pre-tc-ingress filter point).
 * Sibling: TcEgressStats (counterpart).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
