/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * xdp_rx.bpf.c — XDP at NIC RX before kernel stack.
 *
 * STATUS: doc-only stub.  Tier-F.  CNTP rides AF_XDP frames; this
 * is the front door.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Per-flow ingress rate, per-protocol bucket, RSS-queue attribution
 * — all measurable at XDP without kernel-stack overhead.  Also the
 * branch point for AF_XDP redirect (CNTP fast path) vs XDP_PASS
 * (federation control plane up the regular stack).
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: xdp
 * Attachment: `bpf_program__attach_xdp(prog, ifindex)` per-NIC,
 *             native mode (driver-page) when supported, fallback to
 *             generic mode (skb-page) for diag-mode NICs.
 * Returns: XDP_PASS (let kernel stack handle) / XDP_DROP / XDP_TX
 *          (loop back) / XDP_REDIRECT (to AF_XDP socket / cpumap).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - rx_per_proto: ARRAY[N_PROTO=8] — {bytes, packets} per protocol
 *                 (TCP/UDP/ICMP/RDMA-V2/CNTP-magic/SCTP/QUIC/OTHER)
 * - rx_per_rss:   PERCPU_ARRAY[N_QUEUES] — per-RSS-queue
 *                 packet+byte counters (CPU pin obvious)
 * - rx_per_flow:  LRU_HASH[(saddr, daddr, sport, dport, proto) →
 *                          {bytes, packets, first_ts, last_ts}]
 *                 max 65536 — top-K flows for reporting
 * - xsk_redirect_map: BPF_MAP_TYPE_XSKMAP — destination AF_XDP sockets
 *                     keyed by RSS queue
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * No timeline (rate too high).  Counter snapshot via mmap'd ARRAYs.
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-packet: ~20-80 ns (parse Ethernet+IP+L4, look up flow, increment).
 * Event rate: line-rate (10G ≈ 14M pps with 64B packets).
 * Per-sec overhead: <1% of one CPU per 100Mpps.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Native XDP requires NIC driver support.  Most modern NICs (mlx5,
 *   ice, i40e, bnxt, ena) do; some (igb, virtio-net partial) don't.
 *   Falls back to generic mode (~50% slower but still useful).
 * - Per-flow tracking sees only first-fragment — defragmented flow
 *   tracking is out of scope (kernel stack does it post-XDP).
 * - VXLAN / GENEVE encap: parse outer header only by default; opt-in
 *   inner-flow extraction via env var.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: AfXdpFrameStats (XDP_REDIRECT to AF_XDP).
 * Aggregates with: NetifReceive (post-XDP arrival).
 * Aggregates with: SkbDropReason (XDP_DROP doesn't trigger kfree_skb;
 *   our XDP_DROP increments count separately).
 */

#include "../common.h"

/* TODO: implement.  Reference: kernel samples/bpf/xdp1_kern.c. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
