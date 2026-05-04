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
 * Conntrack ships NO TRACE_EVENT macros (verified absence in
 * include/trace/events/, no /sys/kernel/tracing/events/conntrack
 * subsystem).  Real attachment points:
 *
 * BPF program type: kprobe / fentry
 * Attachment points (functions, not tracepoints):
 *   - fentry/__nf_conntrack_alloc        — new conntrack entry
 *   - fentry/nf_conntrack_destroy        — entry destroyed
 *   - fentry/nf_ct_delete                — explicit deletion
 *   - fentry/nf_conntrack_in             — main entry path (per-packet)
 *
 * Plus per-CPU netns counters under /proc/sys/net/netfilter/
 * (`nf_conntrack_count`, `nf_conntrack_max`) polled at startup +
 * snapshots at bench boundaries.
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
