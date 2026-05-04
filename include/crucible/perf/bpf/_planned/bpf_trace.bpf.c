/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * bpf_trace.bpf.c — observe BPF programs' own bpf_printk debug output.
 *
 * STATUS: doc-only stub.  Tier-1 self-observation.  Detect when we (or
 * any other tenant) accidentally ship a BPF program with bpf_printk
 * still wired — those calls hit the kernel's `trace_printk` slow path
 * and cost ~1 µs each (vs ~10 ns for a normal helper call).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * `bpf_printk()` is the BPF-side debug-printf.  It writes to the
 * kernel's `trace_printk` ring buffer via a slow path — fine for
 * debugging, catastrophic in production.  Common deployment failure
 * mode: a BPF program ships with a `bpf_printk("debug %d", x)` left
 * in for a hot tracepoint, costing ~1 µs per invocation × 100K
 * events/sec = 10% CPU all by itself.
 *
 * Manifestation today: we'd see "the host got slower for no reason
 * after a third party loaded a BPF agent" with no observability into
 * which agent or what they're doing.
 *
 * Verified existence on kernel 6.17:
 *   /sys/kernel/tracing/events/bpf_trace/bpf_trace_printk
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified against include/trace/events/bpf.h
 * + /sys/kernel/tracing/events/bpf_trace/ on 6.17):
 *   - tracepoint/bpf_trace/bpf_trace_printk    — fires on ANY bpf_printk call
 *
 * Per-event payload includes the format string (truncated to ~80 bytes).
 * Reader can attribute by truncated-string to the offending program if
 * the format string is unique enough; full attribution requires
 * cross-referencing PERF_RECORD_BPF_EVENT (PerfRecordObserver) for
 * load addresses.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_format: HASH[fmt_truncated[80] → call_count]
 *               LRU_HASH max 256 (different format strings)
 * - global_count: ARRAY[1] — total bpf_printk invocations system-wide
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — recent {fmt_idx, ts_ns}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_bpf_trace_event {
 *     char     fmt_truncated[64];   // first 64 bytes of format string
 *     uint64_t ts_ns;               // WRITTEN LAST
 * };  // 72 B, padded to 128 if cache-line coresidence preferred
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns BPF dispatch + ~50 ns map_update = ~100 ns.
 * Event rate: ZERO on a clean production host (this fires ONLY when
 * somebody's bpf_printk runs).  When non-zero, it's already costing
 * ~1 µs per call from the underlying trace_printk — our 100 ns
 * attribution overhead is in the noise.
 *
 * Effectively free always-on; high signal-to-noise (any non-zero
 * count = somebody shipped debug print to production).
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Format string truncation at 64 bytes loses long format detail.
 * - Doesn't attribute to specific BPF program directly (kernel
 *   tracepoint payload is the format string, not the prog_id).
 *   Cross-reference with PERF_RECORD_BPF_EVENT for prog inventory.
 * - One handler instance suffices system-wide (this is a host signal,
 *   not per-cgroup).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Self-observation triad with: BpfStats (per-program runtime cost),
 *   PerfRecordObserver (PERF_RECORD_BPF_EVENT for prog load/unload),
 *   TracingSubscriberStats (subscriber lag).
 * Detects: third-party debug-leak BPF agents (cilium/tetragon/falco
 *   in noisy modes), Crucible's own dev-mode bpf_printk left in
 *   production by accident.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
