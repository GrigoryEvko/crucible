/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sock_ops.bpf.c — Per-socket TCP RTT/RTO/retransmit telemetry.
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SenseHub aggregates TCP retransmit count + min/max srtt.  Per-socket
 * telemetry — RTT samples, RTO firings, lost-packet attribution — is
 * the input Augur needs for per-peer congestion-control diagnosis.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: sock_ops (BPF_PROG_TYPE_SOCK_OPS)
 * Attach: cgroup-attached; receives callbacks at TCP state changes.
 * Op codes:
 *   - BPF_SOCK_OPS_RTT_CB             — fresh RTT sample
 *   - BPF_SOCK_OPS_RTO_CB             — RTO timer fired
 *   - BPF_SOCK_OPS_RETRANS_CB         — retransmit
 *   - BPF_SOCK_OPS_STATE_CB           — TCP state change
 *   - BPF_SOCK_OPS_TCP_LISTEN_CB      — listen() called
 *   - BPF_SOCK_OPS_TCP_CONNECT_CB     — connect() initiated
 *   - BPF_SOCK_OPS_PARSE_HDR_OPT_CB   — TCP option parse
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_sock_5tuple: LRU_HASH[5tuple → sock_stats]
 *                    {rtt_min_us, rtt_avg_us, rtt_max_us, retrans_count,
 *                     rto_count, last_state, bytes_sent, bytes_received}
 * - rtt_hist:        HASH[peer_ip → log-bucket histogram]
 *                    per-peer RTT distribution
 * - timeline:        ARRAY[1] + BPF_F_MMAPABLE — recent RTO + retrans events
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-callback: ~50-100 ns.
 * Event rate: ~1-100 RTT samples/sec/socket × N sockets.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SenseHub TCP_* counters.
 * Aggregates with: TcpConnLifetime (state changes).
 */

#include "../common.h"

/* TODO: implement.  Reference: kernel selftests/bpf/progs/test_tcp_rtt.c. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
