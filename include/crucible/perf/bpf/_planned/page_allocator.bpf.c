/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * page_allocator.bpf.c — kmem/mm_page_alloc per zone × per order × per GFP.
 *
 * STATUS: doc-only stub.  Tier-C.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Page allocator behavior dominates "where did our perf go" for any
 * workload that allocates: per-zone exhaustion (DMA/DMA32 vs Normal
 * vs Movable), per-order pressure (orders 4+ trigger compaction),
 * per-GFP-flag stalls (GFP_ATOMIC failing under pressure cascades into
 * IRQ-context pain).  SenseHub gets a summary count via the cheap
 * tracepoint; per-(zone, order) breakdown needs hash maps.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/kmem/mm_page_alloc           — page allocated
 *   - tracepoint/kmem/mm_page_free            — page returned
 *   - tracepoint/kmem/mm_page_alloc_zone_locked — zone locked
 *   - tracepoint/kmem/mm_page_alloc_extfrag   — fragmentation event
 *     (already in SenseHub)
 *   - tracepoint/kmem/mm_page_pcpu_drain      — per-CPU page drain
 * Kernel min: 4.0.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - alloc_by_zone_order: ARRAY[ZONES × ORDERS] — fixed-shape histogram
 *                        (zones: DMA, DMA32, Normal, HighMem, Movable,
 *                         Device — 6 zones; orders 0-10 = 11 — total 66)
 * - alloc_by_gfp:        ARRAY[N_FLAGS] — per-flag bucket (KERNEL,
 *                        ATOMIC, USER, HIGHUSER, NOWAIT, NORETRY, ...)
 * - alloc_fail:          ARRAY[1] — failed-alloc counter (mm_page_alloc
 *                        with order ≥ MAX_ORDER or ZONE_MOVABLE
 *                        exhaustion)
 * - hot_call_sites:      LRU_HASH[stack_id → count] — top-K
 *                        allocation call sites
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * No timeline (rate too high — millions/sec on busy hosts).  Userspace
 * reads aggregated histograms at bench end.
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns.
 * Event rate: 100K-10M alloc/sec on a busy compute host.
 * Per-sec overhead: 1-10% with naive instrumentation — TOO HIGH.
 * Mitigation: sample-period filter (record every Nth event) — set
 * via env var `CRUCIBLE_PERF_PAGE_ALLOC_SAMPLE=N` (default 16).
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Sample period defaults to 1-in-16 to keep overhead < 1%.
 *   Histograms become "samples × period" → user multiplies for
 *   absolute counts.
 * - System-wide (page allocator is global) but `is_target()`-filtered
 *   on emission to attribute to OUR allocations.  Cross-process
 *   pressure visible in SenseHub `_RESERVED_38`-style counter (or
 *   wire a separate SYSTEM_PAGE_ALLOC counter).
 * - mm_page_alloc fires on EVERY page-cache fill — high-frequency.
 *   Filter at BPF (skip GFP_HIGHUSER_MOVABLE without our tgid)
 *   to bound rate.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Promoted from: SenseHub (per-(zone, order) needs hash, can't fit Idx).
 * Aggregates with: vmscan_ext (alloc fails → kswapd wakes).
 * Aggregates with: PmuUncoreImc (alloc bandwidth contributes to BW).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
