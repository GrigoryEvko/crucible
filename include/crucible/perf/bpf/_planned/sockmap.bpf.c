/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sockmap.bpf.c — sk_msg/sk_skb sockmap forwarding observation.
 *
 * STATUS: doc-only stub.  Tier-F.  CNTP could use sockmap to route
 * messages between local sockets without copying through userspace.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * sockmap (kernel 4.14+) lets BPF redirect packets between sockets
 * in-kernel — bypassing userspace parsing.  Useful for CNTP gateway
 * routing local-tenant traffic without the cost of userspace
 * read/write loops.  This facade observes which redirects fire (and
 * which DON'T, falling back to userspace).
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program types:
 *   - sk_msg     — process userspace sendmsg() before going on the wire
 *   - sk_skb/stream_verdict — process inbound skb at sockmap delivery
 *   - sk_skb/stream_parser  — message parser for sockmap
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - sock_map: BPF_MAP_TYPE_SOCKMAP — local socket FDs for redirect
 * - per_redirect: ARRAY[N_VERDICTS] — {SK_PASS, SK_DROP, SK_REDIRECT,
 *                 SK_REDIRECT_MISS} counters per verdict
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-message: ~50-100 ns sk_msg parse.  Sockmap redirect saves the
 * userspace context-switch cost (~3-5 µs per message).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SockOpsObserver (per-socket TCP state).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
