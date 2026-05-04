/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * module_lifecycle.bpf.c — kernel module load/unload events.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-2 addition.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Mid-bench surprise: the kernel suddenly behaves differently because
 * a module just loaded (e.g., systemd-modules-load triggers an out-of-
 * tree NIC driver after the initial bench window).  Effects:
 *   - new tracepoints become available (changes BPF auto-attach)
 *   - new sysctls exposed (changes runtime behavior)
 *   - new IRQs allocated (changes IRQ topology)
 *   - module init may stall a CPU briefly (~100 µs - 10 ms)
 *
 * Conversely, mid-bench unload (rmmod) breaks any kprobe attached
 * to that module's symbols — silent failure if not observed.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/module/module_load    — module loaded
 *   - tracepoint/module/module_free    — module unloaded
 *   - tracepoint/module/module_get     — refcount up
 *   - tracepoint/module/module_put     — refcount down
 *   - tracepoint/module/module_request — auto-load triggered
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - load_events: ARRAY[1] + BPF_F_MMAPABLE — every load/free event
 * - active_modules: HASH[name[56] → load_ts] — currently-loaded set
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: typically 0/sec on stable host.
 * Effectively free always-on.
 *
 * Bench harness emits banner on detection:
 *   *** MODULE LOADED nf_conntrack_netlink AT 8.4s — kernel behavior
 *       may have changed; bench results before/after split ***
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Modules built into the kernel (modprobe -l vs lsmod) don't fire
 *   these events; they're loaded at boot before BPF can attach.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Bench-reliability pair with: clocksource.bpf.c (similar single-event
 *   bench-disruption category — rare, high-impact, currently invisible).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
