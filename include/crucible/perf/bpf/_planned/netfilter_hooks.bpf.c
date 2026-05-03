/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * netfilter_hooks.bpf.c — netfilter hook events (kernel 6.4+ BPF nf prog).
 *
 * STATUS: doc-only stub.  Tier-F.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * BPF can attach to netfilter hooks directly (PRE_ROUTING, FORWARD,
 * etc.) since kernel 6.4 — same hook points as iptables/nftables but
 * counter-only observation rather than policy enforcement.  Useful
 * for "how much traffic was dropped at firewall stage" attribution.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: BPF_PROG_TYPE_NETFILTER (kernel 6.4+)
 * Attach: BPF_LINK_TYPE_NETFILTER targeting (pf, hooknum, priority).
 * Hooks: PRE_ROUTING / LOCAL_IN / FORWARD / LOCAL_OUT / POST_ROUTING.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_hook: ARRAY[N_HOOKS=5] — {packets, bytes, accept, drop, stolen}
 *             per hook point
 * - drop_by_chain: HASH[chain_name → count] — top-K dropping chains
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-packet: ~50 ns extra per hook (we add to existing nf_hook chain).
 * Event rate: every packet through netfilter.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: NfConntrack (conntrack-side observation).
 * Sibling: TcEgressStats (post-netfilter accounting).
 */

#include "../common.h"

/* TODO: implement (kernel 6.4+ BPF_PROG_TYPE_NETFILTER). */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
