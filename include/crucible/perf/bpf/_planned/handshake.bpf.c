/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * handshake.bpf.c — kernel TLS handshake upcall events.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Verified
 * against /sys/kernel/tracing/events/handshake/ on 6.17 (16 events).
 * SUPERSEDES the partial coverage in `ktls_offload.bpf.c` plan
 * (which Agent 2 incorrectly claimed `tls_alert_*` doesn't exist —
 * they exist under handshake/, not tls/).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Federation mTLS handshake setup (kernel-orchestrated TLS, used
 * with kTLS for record-layer offload) goes through a userspace
 * upcall protocol: the kernel asks userspace ("the handshake daemon",
 * e.g. tlshd or a Crucible-managed equivalent) to perform the actual
 * TLS handshake, then takes over the established session.
 *
 * Failures in this protocol show up as:
 *   - Handshake submitted but never completed (timeout, daemon dead)
 *   - Handshake cancelled (peer disconnected mid-handshake)
 *   - TLS alerts received post-handshake (peer terminated session)
 *
 * Currently invisible to the application — kernel logs to dmesg,
 * federation just sees "connection failed" with no upstream signal.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17 — ALL 16 events real):
 *   handshake lifecycle:
 *   - tracepoint/handshake/handshake_submit          — handshake requested
 *   - tracepoint/handshake/handshake_submit_err      — submit failed
 *   - tracepoint/handshake/handshake_complete        — handshake succeeded
 *   - tracepoint/handshake/handshake_cancel          — handshake cancelled
 *   - tracepoint/handshake/handshake_cancel_busy     — cancel during work
 *   - tracepoint/handshake/handshake_cancel_none     — cancel but no work
 *   - tracepoint/handshake/handshake_destruct        — handshake torn down
 *   - tracepoint/handshake/handshake_notify_err      — notify failure
 *
 *   handshake-daemon command interface:
 *   - tracepoint/handshake/handshake_cmd_accept      — daemon accepted job
 *   - tracepoint/handshake/handshake_cmd_accept_err  — daemon accept failed
 *   - tracepoint/handshake/handshake_cmd_done        — daemon completed job
 *   - tracepoint/handshake/handshake_cmd_done_err    — daemon job failed
 *
 *   TLS alerts (post-handshake — these are why this stub exists):
 *   - tracepoint/handshake/tls_alert_recv            — TLS alert received
 *   - tracepoint/handshake/tls_alert_send            — TLS alert sent
 *   - tracepoint/handshake/tls_contenttype           — TLS content type
 *
 * Per-event payload includes socket cookie (correlates with
 * sock_ops.bpf.c's per-socket data) and alert level/description for
 * tls_alert_* events.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_socket_handshake: HASH[sock_cookie → {state, submit_ts,
 *                                              complete_ts, alert_count}]
 *                         LRU_HASH max 4096
 * - alert_count_by_desc: HASH[alert_description → count]
 *                        TLS alert descriptions enum (close_notify,
 *                        bad_record_mac, decryption_failed, ...)
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_handshake_event {
 *     uint64_t sock_cookie;
 *     uint16_t event_kind;       // HandshakeEventKind enum
 *     uint8_t  alert_level;      // 0 if non-alert event
 *     uint8_t  alert_desc;       // 0 if non-alert event
 *     uint8_t  _pad[4];
 *     uint64_t ts_ns;            // WRITTEN LAST
 * };  // 24 B
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: 1-1000/sec depending on TLS
 * handshake rate (typically low — handshakes are setup-time, not
 * data-path).  Per-sec overhead: <0.01%.  Default-on safe.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Handshake subsystem activated only when kernel mode-set TLS or
 *   handshake-upcall is configured.  Tracepoints fire only on those
 *   handshakes — pure-userspace TLS (OpenSSL not delegated to kernel)
 *   bypasses these.
 * - Alert payload contains TLS alert codes per RFC 5246 §7.2;
 *   userspace decoder needed for human-readable description.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * SUPERSEDES partial coverage in: ktls_offload.bpf.c (which is now
 *   updated to point here for alert observation).
 * Sibling: sock_ops.bpf.c (per-socket TCP RTT/RTO) — sock_cookie joins.
 * Sibling: tcp_lifetime.bpf.c (connection lifecycle) — handshake_destruct
 *   correlates with tcp_destroy_sock.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
