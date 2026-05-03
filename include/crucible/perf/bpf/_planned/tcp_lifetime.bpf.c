/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * tcp_lifetime.bpf.c — TCP connection lifecycle: destroy_sock + rcv_space + loss_probe.
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SenseHub gets TCP state via inet_sock_set_state.  Connection
 * destruction and per-connection RX-window adjustments and TLP
 * (Tail Loss Probe) firings are additional signals: short-lived
 * connection storms (federation control plane), RX window starvation
 * (sender out-paced our consumer), TLP cascades (path going bad).
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/tcp/tcp_destroy_sock         — connection torn down
 *   - tracepoint/tcp/tcp_rcv_space_adjust     — RX window expanded
 *   - tracepoint/tcp/tcp_send_loss_probe      — TLP fired
 *   - tracepoint/tcp/tcp_receive_reset        — RST received
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_peer:    LRU_HASH[peer_ip → {connect_count, destroy_count,
 *                                     rst_recv, tlp_fires, rcv_space_adj}]
 * - lifetime_hist: HASH[bucket → count] — connection lifetime
 *                  histogram (1ms to 1h, log-bucketed)
 * - timeline:    ARRAY[1] + BPF_F_MMAPABLE — recent {peer_ip, event_type,
 *                lifetime_ns, ts_ns}
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100 ns.  Event rate: typically <1K/sec.  Cheap.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SockOpsObserver (per-socket RTT / RTO).
 * Sibling: SenseHub `TCP_*` aggregates.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
