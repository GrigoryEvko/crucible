/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * nf_conntrack.bpf.c — netfilter conntrack tracepoints (federation NAT).
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Federation traffic crossing stateful NAT (cloud egress, k8s NodePort,
 * VPN gateway) gets conntrack table entries.  Table fill, expiry,
 * GC, lookup latency — all directly impact federation throughput
 * when conntrack saturates.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/conntrack/destroy
 *   - tracepoint/conntrack/new
 *   - tracepoint/conntrack/update
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - conntrack_stats: ARRAY[1] — {new_count, destroy_count, update_count,
 *                                table_size_estimate}
 * - per_proto: ARRAY[N_PROTO] — per-protocol conntrack rate
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Bounded by federation connection rate.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: TcpConnLifetime (TCP-specific lifecycle; this is L3
 *   conntrack, includes UDP, ICMP, etc.).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
