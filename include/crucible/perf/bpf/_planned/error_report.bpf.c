/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * error_report.bpf.c — kernel-detected error report observation.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Bench-
 * reliability disaster early warning.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * The kernel's `error_report` subsystem (verified on 6.17 — single
 * tracepoint `error_report_end`) fires when KASAN, KFENCE, KMSAN, slab
 * corruption detection, page-table corruption check, or any other
 * kernel-internal error-detector emits a report.  Manifestation in
 * production today: kernel logs a long stack trace to dmesg, our bench
 * silently runs onward, results are invalidated by the underlying
 * memory corruption.  We have no facade that detects "the kernel just
 * found something wrong with itself" mid-bench.
 *
 * Key insight: error_report_end fires AFTER the report is fully written
 * to dmesg.  Subscribing tells us the kernel detected a real error
 * (not just any scary log line — those can be benign).
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17):
 *   - tracepoint/error_report/error_report_end   — error report finished
 *
 * Per-event payload includes the error tool name (from `enum
 * error_detector` in include/linux/error-injection.h):
 *   - KASAN  — Kernel Address Sanitizer (use-after-free, OOB)
 *   - KFENCE — KFENCE allocator-poisoning sampler
 *   - WARN   — kernel WARN_ON_*  (broad)
 *   - UBSAN  — Kernel UBSAN
 *   - SOFT_LOCKUP / HUNG_TASK detectors
 *
 * Plus identifying info: error address, allocator stack id (when KASAN/KFENCE).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_detector: HASH[detector_name → fire_count]
 * - report_timeline: ARRAY[1] + BPF_F_MMAPABLE — every fired report
 *                    with {detector, addr, ts_ns}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_error_report_event {
 *     uint64_t addr;            // error address (when applicable)
 *     uint8_t  detector;        // ErrorDetector enum
 *     uint8_t  _pad[7];
 *     uint64_t ts_ns;           // WRITTEN LAST
 * };  // 24 B
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: typically 0/sec on healthy kernel.
 * Effectively free always-on.
 *
 * Bench harness emits banner on detection:
 *   *** KASAN REPORT AT 8.4s — bench results AFTER THIS POINT
 *       MEASURED ON A KERNEL THAT JUST DETECTED MEMORY CORRUPTION ***
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Requires CONFIG_KASAN=y / CONFIG_KFENCE=y / CONFIG_UBSAN=y for the
 *   detectors to fire.  KASAN is rare in production builds (high
 *   overhead); KFENCE is enabled in many distro kernels (low overhead
 *   sampler).
 * - error_report_end fires AFTER the report is logged; doesn't help
 *   prevent the offending operation, only flags it for post-bench
 *   forensics.
 * - dmesg correlation needed to pull the full stack trace; this
 *   tracepoint just gives the trigger event.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Bench-reliability pair with: clocksource.bpf.c (DEPRIORITIZED) and
 *   module_lifecycle.bpf.c — all in the "rare but invalidating
 *   single-event" category.
 * Drives: bench harness's "INVALIDATED" banner inline output.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
