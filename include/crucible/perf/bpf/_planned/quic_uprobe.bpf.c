/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * quic_uprobe.bpf.c — QUIC library uprobe (federation transport).
 *
 * STATUS: doc-only stub.  Tier-F.  Library choice deferred to
 * implementation time (lsquic / msquic / quiche / cloudflare-quiche).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * QUIC is in userspace (no kernel tracepoint coverage).  We attach
 * uprobes to the chosen QUIC library's hot symbols to measure:
 * stream open/close, packet send/recv, congestion-controller
 * transitions, 0-RTT vs 1-RTT split.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: uprobe / uretprobe
 * Attach: per-shared-library uprobe on chosen entry points (e.g.,
 *   `uprobe:/usr/lib/liblsquic.so:lsquic_engine_packet_in`).
 * Modern: `uprobe.multi` for batched attach.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_conn:    LRU_HASH[(conn_id) → {streams_open, packets_in,
 *                                       packets_out, retx_count, cc_state}]
 * - cc_transitions: ARRAY[N_CC_STATES] — per-state count
 *                   (Slow-Start, Cong-Avoid, Recovery, App-Limited)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~2-5 µs (uprobe TRAP to BPF — relatively expensive vs
 * kprobe/fentry).  Gate to opt-in via env var; not always-on.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Library-version-dependent symbol names; need a per-version
 *   attach table.
 * - Uprobe overhead is significant; sample-period gating recommended.
 * - kfunc-equivalent for QUIC doesn't exist (kernel doesn't know
 *   about QUIC); uprobe is the only path.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SockOpsObserver (UDP socket layer underneath QUIC).
 * Sibling: TcEgressStats (post-QUIC-encryption packet counts).
 */

#include "../common.h"

/* TODO: implement, choose QUIC library, decide symbol coverage. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
