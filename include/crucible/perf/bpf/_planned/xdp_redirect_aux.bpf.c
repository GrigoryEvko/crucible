/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * xdp_redirect_aux.bpf.c — XDP redirect auxiliary tracepoints (devmap, cpumap, exception).
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-2 addition.  Auxiliary to
 * planned xdp_rx.bpf.c (Tier-F).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * XDP_REDIRECT verdict can route a frame to a devmap entry (other NIC),
 * cpumap entry (specific CPU for kernel-stack handling), or AF_XDP
 * socket.  Exceptions (no-target, capacity, drop) all silently happen.
 * The 4 tracepoints expose:
 *   - WHERE redirected frames went
 *   - WHEN drops happened on the redirect path
 *   - HOW MANY were lost vs successful
 *
 * For CNTP (AF_XDP-based) tail-latency analysis, "10% of redirects to
 * AF_XDP failed" is the answer to "why is throughput plateauing".
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/xdp/xdp_devmap_xmit       — redirect to devmap dispatched
 *   - tracepoint/xdp/xdp_cpumap_kthread    — cpumap kernel-thread processed
 *   - tracepoint/xdp/xdp_cpumap_enqueue    — frame enqueued to cpumap
 *   - tracepoint/xdp/xdp_exception         — XDP_ABORTED / verifier-fail
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_redirect_target: HASH[(src_ifindex, dst_ifindex) → count]
 * - per_exception: HASH[reason → count]
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: scales with packet rate; can be
 * 100K-10M/sec on busy NIC.  Per-sec overhead: 0.5-50% — too high
 * for always-on.  Default-off; opt-in via `CRUCIBLE_PERF_XDP_AUX=1`.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Sample-period gating recommended; 1-in-1024 events suffices for
 *   distribution stats.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: xdp_rx.bpf.c (planned, Tier-F) — RX-side XDP traffic.
 * Sibling: af_xdp_frame.bpf.c (planned, Tier-F) — AF_XDP frame
 *   accounting (CNTP fast path).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
