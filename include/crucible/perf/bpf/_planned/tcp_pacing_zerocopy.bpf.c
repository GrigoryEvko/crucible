/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * tcp_pacing_zerocopy.bpf.c — TCP pacing decisions + MSG_ZEROCOPY events.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-2 addition.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Two related-but-distinct TCP optimizations whose effectiveness is
 * currently invisible:
 *
 * 1. BBR-style pacing — kernel paces sends to avoid burst-induced
 *    queue buildup.  Per-socket pacing rate, pacing-rate adaptation
 *    decisions exposed via `tcp/tcp_send_*` / fq qdisc tracepoints.
 *
 * 2. MSG_ZEROCOPY — userspace-supplied buffers go DMA-direct from
 *    user pages, no kernel-space copy.  Requires careful page lifetime
 *    management; kernel emits completion events (errqueue) when zero-
 *    copy paths complete and pages can be released.  Failed offloads
 *    silently fall back to copy.
 *
 * Federation TCP throughput depends on both.  Currently invisible.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * Neither subsystem ships dedicated tracepoints (verified against
 * include/trace/events/tcp.h and sock.h on 6.17 — no `tcp_pacing_*`
 * or `sock_msg_zerocopy_*` events).  Real attach points:
 *
 * BPF program type: kprobe / fentry + sock_ops
 * Attachment points:
 *   - fentry/sk_pacing_shift_update         — pacing rate adjusted
 *   - fentry/tcp_pacing_check               — per-skb pacing decision
 *   - fentry/__msg_zerocopy_callback        — zerocopy completion
 *   - fentry/sock_zerocopy_realloc          — zerocopy buffer realloc
 *   - sock_ops with BPF_SOCK_OPS_BASE_RTT_CB / RTT_CB for pacing-rate
 *     adaptation observation
 *
 * Userspace alternative: read `ss -tieo` for per-socket pacing rate +
 * snd_zc / app_limited flags (cheap polling at bench boundaries).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_socket_pacing: LRU_HASH[(src_ip, src_port) → {rate_bps,
 *                                                      adjust_count}]
 * - zerocopy_status: HASH[(socket_addr) → {success_count, fallback_count}]
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100 ns.  Event rate: ~1-100/sec per socket.
 * Effectively free for 100-1000 sockets.  Default-off; opt-in via
 * `CRUCIBLE_PERF_TCP_ADV=1`.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - MSG_ZEROCOPY user-side completion events come via socket errqueue;
 *   kernel-side events catch only the kernel half of the lifecycle.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: sock_ops.bpf.c (planned) — per-socket RTT/RTO/retransmit
 *   (BPF_PROG_TYPE_SOCK_OPS).
 * Sibling: tcp_lifetime.bpf.c (planned) — connection lifecycle.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
