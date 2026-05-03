/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * netif_receive.bpf.c — net/netif_receive_skb kernel-stack arrival latency.
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Frame timestamp at NIC RX (XDP) → kernel-stack arrival
 * (netif_receive_skb) → socket queue (sk_data_ready).  Per-stage
 * latency exposes "kernel stack stalled this frame", e.g. softirq
 * starvation or RPS misconfiguration.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/net/netif_receive_skb     — entered kernel stack
 *   - tracepoint/net/netif_rx              — pushed to RX backlog
 *   - tracepoint/net/napi_gro_receive_entry — GRO aggregation receive
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_dev_lat: HASH[ifindex → log-bucket histogram]
 *                arrival latency distribution (XDP_PASS → netif_receive)
 * - per_proto:   ARRAY[N_PROTO] — per-protocol packet count
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-packet: ~50 ns.
 * Event rate: line-rate (every received frame).  Gate to one-per-N
 *   sample under high pps.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: NapiPoll (driver poll → frames produced).
 * Sibling: TcIngressStats (post-arrival classification).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
