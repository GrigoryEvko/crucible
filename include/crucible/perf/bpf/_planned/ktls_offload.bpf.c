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
 * Attachment points:
 *   - tracepoint/tls/tls_device_offload_set        — offload negotiated
 *   - tracepoint/tls/tls_device_decrypted          — per-record decrypt
 *   - tracepoint/tls/tls_device_rx_resync_send     — RX resync needed
 *   - tracepoint/tls/tls_alert_send/recv           — TLS alert
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
