/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * fib_lookup.bpf.c — IPv4 + IPv6 routing decision attribution.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Verified
 * against /sys/kernel/tracing/events/fib/fib_table_lookup and
 * /sys/kernel/tracing/events/fib6/fib6_table_lookup on 6.17.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Federation cold-start latency is dominated by routing decisions:
 *   - First packet to a new peer triggers FIB lookup (kernel routing table)
 *   - On failure: ARP / NDP resolution, gateway lookup, sometimes DNS
 *   - Every subsequent packet hits cached route (much faster)
 *
 * For Crucible's federation cold-start ("how long until peer N reachable
 * from this node"), the FIB lookup latency is one of several
 * contributing factors.  Currently invisible — federation tells us
 * "connect succeeded" or "failed", not "took 8 ms because we had to
 * resolve a multi-hop gateway path".
 *
 * Mid-bench: route flap (BGP withdrawal, link-local hand-off, k8s
 * service IP migration) fires lookup events; observable here.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17):
 *   - tracepoint/fib/fib_table_lookup     — IPv4 FIB lookup
 *   - tracepoint/fib6/fib6_table_lookup   — IPv6 FIB lookup
 *
 * Per-event payload includes lookup-key (src/dst IP, ToS, oif, mark)
 * and result (gateway, output device, scope, error code).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_dst_summary: LRU_HASH[(dst_ip, family) → {lookup_count,
 *                                                   last_result_dev,
 *                                                   error_count}]
 *                    LRU max 4096 (federation peer set typically <1000)
 * - error_by_code: HASH[err_code → count]
 *                  err_code: ENETUNREACH, EHOSTUNREACH, ENETDOWN, etc.
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_fib_lookup_event {
 *     uint8_t  family;             // AF_INET / AF_INET6
 *     uint8_t  result;             // 0=ok, errno otherwise
 *     uint16_t out_dev_idx;        // ifindex of output device (or 0)
 *     uint8_t  dst_addr[16];       // IPv4 in low 4 bytes if INET
 *     uint64_t ts_ns;              // WRITTEN LAST
 * };  // 32 B
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: depends — every connect, every
 * first-packet-to-new-dst.  Steady-state: 10-1000/sec on multi-peer
 * federation node.  Per-sec overhead: <0.01%.  Default-on safe.
 *
 * Spike rate during fleet startup: 10K-100K/sec for ~seconds during
 * peer enumeration.  Sample-period gating recommended for bursts.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Tracepoint fires for table-lookup itself; doesn't capture cache
 *   hits in route cache (those bypass the table lookup).  For
 *   per-flow latency including cache hits, sock_ops.bpf.c gives
 *   per-connect timing instead.
 * - Multi-table routing (IP rule / VRF) requires additional table_id
 *   payload field; check kernel version for its presence.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: sock_ops.bpf.c (per-connection RTT — pairs with this for
 *   "FIB lookup → first packet RTT" attribution).
 * Sibling: tcp_lifetime.bpf.c (connect → established handshake timing).
 * Sibling: nf_conntrack.bpf.c (planned) — conntrack table population
 *   correlates with FIB-determined routes.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
