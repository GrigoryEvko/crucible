/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * iter_sock.bpf.c — Walk every socket for connection inventory.
 *
 * STATUS: doc-only stub.  Tier-G.  BPF iter (kernel 5.8+).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Periodic socket inventory: who's connected to who, byte counts,
 * congestion-control name per socket, RTT, RX/TX queue depth.
 * Replaces `ss -tn` system-call-heavy enumeration with one BPF walk.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: BPF_TRACE_ITER targeting ITER_TYPE_TCP / UDP / UNIX
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct sock_iter_record {
 *     __u8  family;            // AF_INET / AF_INET6 / AF_UNIX
 *     __u8  protocol;          // IPPROTO_TCP / UDP
 *     __u8  state;             // TCP state
 *     __u8  _pad;
 *     __u32 src_port, dst_port;
 *     __u8  src_addr[16], dst_addr[16];   // v4-mapped if needed
 *     __u64 bytes_sent, bytes_recv;
 *     __u32 srtt_us, snd_cwnd;
 *     __u32 rx_queue, tx_queue;
 *     char  cc_name[16];       // BBR / CUBIC / etc.
 * };
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-socket: ~200 ns.  N_sockets typically 100-10K → 20µs-2ms total.
 * Run periodically (1 Hz default).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SockOpsObserver (per-event socket telemetry; this is
 *   inventory).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
