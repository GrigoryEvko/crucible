/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * ktls_offload.bpf.c — KTLS hardware offload events (federation mTLS).
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Federation mTLS uses kernel TLS with NIC-side AES offload (when
 * supported: mlx5, ixgbe, ice).  Failed offload → fallback to
 * kernel/userspace AES → 5-10× CPU cost increase.  KTLS tracepoints
 * surface the offload setup + per-record state, including
 * fallback-to-software events.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified against net/tls/trace.h on 6.17 for the
 * `tls/` subsystem AND /sys/kernel/tracing/events/handshake/ for TLS
 * alerts — alerts ARE exposed as tracepoints under handshake/, not tls/):
 *   - tracepoint/tls/tls_device_offload_set        — offload negotiated
 *   - tracepoint/tls/tls_device_decrypted          — per-record decrypt
 *   - tracepoint/tls/tls_device_rx_resync_send     — RX resync needed
 *   - tracepoint/tls/tls_device_rx_resync_nh_delay — resync via NH delay
 *   - tracepoint/tls/tls_device_rx_resync_nh_schedule
 *   - tracepoint/tls/tls_device_tx_resync_req      — TX resync requested
 *   - tracepoint/tls/tls_device_tx_resync_send     — TX resync sent
 *   - tracepoint/handshake/tls_alert_recv          — TLS alert received
 *   - tracepoint/handshake/tls_alert_send          — TLS alert sent
 *   - tracepoint/handshake/tls_contenttype         — TLS content type
 * (Per-Crucible audit-round-3 correction: prior stub claimed `tls_alert_*`
 *  did not exist anywhere — they DO exist under the handshake subsystem.
 *  Sibling: handshake.bpf.c covers the full handshake/ subsystem (16 events).)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - offload_state: HASH[sock_id → {hw_offload, sw_fallback_count,
 *                                   resync_count, alert_send, alert_recv}]
 * - global_stats:  ARRAY[1] — total {records_decrypted, fallbacks, alerts}
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Bounded by TLS handshake / record rate.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SockOpsObserver (TLS encapsulates TCP).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
