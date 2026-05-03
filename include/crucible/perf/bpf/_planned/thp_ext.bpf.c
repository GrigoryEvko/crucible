/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * thp_ext.bpf.c — Extended THP collapse/split per reason with stack attribution.
 *
 * STATUS: doc-only stub.  Tier-C.  SenseHub has aggregate
 * `THP_COLLAPSE_OK` / `THP_COLLAPSE_FAIL`; this adds the WHY.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Transparent Huge Pages collapse failures cascade to TLB pressure
 * (4K pages instead of 2M), and splits cascade to allocator pressure
 * (split = 512× more page-table churn).  WHY collapses fail (memcg
 * pressure, NUMA imbalance, fragmentation) and WHO triggered the split
 * (madvise, fork, swap-out) decides the mitigation.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/huge_memory/mm_collapse_huge_page         — collapse attempt
 *   - tracepoint/huge_memory/mm_collapse_huge_page_isolate — isolate stage
 *   - tracepoint/huge_memory/mm_collapse_huge_page_swapin  — swap-in stage
 *   - tracepoint/huge_memory/mm_split_huge_pmd             — PMD split
 *   - tracepoint/huge_memory/mm_split_huge_pud             — PUD split
 *   - tracepoint/huge_memory/mm_khugepaged_scan_pmd        — khugepaged scan
 * Args carry: status enum, address, mm pointer.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - collapse_by_status: ARRAY[N_STATUS=18] — per-status counter
 *                       (SCAN_FAIL, SCAN_PMD_NULL, SCAN_LACK_REFERENCED_PAGE,
 *                        SCAN_PAGE_RO, SCAN_PAGE_NULL, SCAN_PAGE_COUNT,
 *                        SCAN_PAGE_LRU, SCAN_PAGE_LOCK, etc.)
 * - split_by_caller:    HASH[stack_id → count] — top-K split-trigger
 *                       call sites (LRU_HASH, max 256)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.
 * Event rate: 1-100/sec; khugepaged_scan can hit 100K/sec under heavy
 * scanning — gate scan_pmd to summary-only (no stack capture) and
 * collect stacks only on collapse/split.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SenseHub `THP_COLLAPSE_OK`/`THP_COLLAPSE_FAIL` (aggregate).
 * Aggregates with: PmuSample DTLB-miss (4K-page TLB pressure when
 *   THP collapses fail).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
