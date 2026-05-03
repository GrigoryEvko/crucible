/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * tc_egress.bpf.c — tc-bpf egress: per-class CNTP shaping accounting.
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Egress is where shaping/queueing happens.  Per-class accounting
 * (CNTP control vs CNTP bulk vs federation mTLS vs uncategorized)
 * lets us attribute "outgoing bandwidth saturated by class X" or
 * "qdisc dropped Y bytes/sec".
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: sched_cls (tc-bpf classifier)
 * Attach: `tc filter add dev ethN egress bpf direct-action`
 *         (loaded via libbpf's bpf_tc_attach)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_class:    ARRAY[N_CLASSES=8] — {bytes, packets, drops}
 *                 classes: CNTP_CTRL, CNTP_BULK, FED_MTLS, FED_QUIC,
 *                          MGMT, OTHER, OVERFLOW, RESERVED
 * - per_dst_ip:   LRU_HASH[dst_ipv6 → bytes] — top-K egress destinations
 * - tx_drops_by_reason: ARRAY[N_DROP_REASONS] — qdisc-side drop reasons
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-packet: ~30-50 ns.  Per-NIC; egress sees every transmit.
 * Event rate: line-rate.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: TcIngressStats (counterpart on RX).
 * Sibling: QdiscBacklog (per-qdisc queue depth — tc-bpf sees post-classifier).
 */

#include "../common.h"

/* TODO: implement.  Reference: BCC tcpaccept-style tc-bpf samples. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
