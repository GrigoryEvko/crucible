/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * damon.bpf.c — DAMON kernel-side aggregation events.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Verified
 * against /sys/kernel/tracing/events/damon/ on 6.17 (4 events).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * DAMON (Data Access MONitor, kernel 5.15+) maintains per-region
 * access counters via PG_referenced bit scanning.  At each
 * `aggr_us` interval (default 100 ms), it emits per-region access
 * counts and, when DAMOS schemes are configured, fires per-scheme
 * apply events.
 *
 * The userspace facade (`DamonReader.md`) consumes the SAME data via
 * sysfs polling — but BPF attachment lets us:
 *   - Get sub-aggregation-window callback (faster than userspace polling)
 *   - Filter regions in BPF (only emit "hot" regions, drop cold)
 *   - Cross-correlate with other tracepoints in real time
 *   - Drive Augur scheme-application decisions in-kernel
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17 — 4 events):
 *   - tracepoint/damon/damon_aggregated         — per-region count emitted
 *   - tracepoint/damon/damon_monitor_intervals_tune — auto-tuning step
 *   - tracepoint/damon/damos_before_apply       — DAMOS scheme about to apply
 *   - tracepoint/damon/damos_esz                — extended size info
 *
 * Per `damon_aggregated` event, payload includes target_id, region_id,
 * region start/end, nr_accesses (count this aggregation window),
 * nr_accesses_aggr (accumulated across windows).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - hot_regions: HASH[region_addr → {accesses_total, last_aggr_ts}]
 *                LRU_HASH max 1024
 * - per_target_summary: HASH[target_id → {region_count, total_accesses}]
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — per-region {start, end,
 *             nr_accesses, ts_ns}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_damon_event {
 *     uint64_t region_start;
 *     uint64_t region_end;
 *     uint32_t nr_accesses;
 *     uint16_t target_id;
 *     uint8_t  _pad[2];
 *     uint64_t ts_ns;          // WRITTEN LAST
 * };  // 32 B (cache-line coresident)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns × N regions per aggregation window.  Default
 * aggr_us=100 ms × 100 regions = 1000 events/sec total = 50 µs/sec
 * BPF overhead = 0.005% CPU.  Effectively free always-on.
 *
 * Userspace cost is tiny by comparison; BPF flavor wins when filtering
 * (only emit cold→hot transitions) is desired in-kernel.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Requires CONFIG_DAMON=y + a DAMON kdamond actively running.
 *   Bench harness must configure a DAMON context targeting the
 *   process at startup (see DamonReader.md for the sysfs setup).
 * - Tracepoints became BPF-attachable in kernel 6.x; pre-6.x kernels
 *   exposed only via the legacy DAMON_DBGFS interface.
 * - DAMOS scheme tracepoints fire only when schemes are configured;
 *   for pure observation (no applied actions) only damon_aggregated
 *   matters.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Userspace pair: DamonReader.md (sysfs polling for the same data).
 * Cold-path complement to: PmuSample DTLB-miss IP sampling.
 * Drives: Cipher cold-tier promotion + NUMA migration recommendations.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
