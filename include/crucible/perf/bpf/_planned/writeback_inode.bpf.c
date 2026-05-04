/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * writeback_inode.bpf.c — per-inode writeback attribution.
 *
 * STATUS: doc-only stub.  Tier-1 audit-round-2 addition.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * "Which file is being written back during our bench window?" —
 * for Cipher cold-tier diagnostics.  Distinguishes "OUR Cipher
 * writes" from "another tenant's syslog churn" when both share the
 * page cache.  SenseHub's `WRITE_THROTTLE_JIFFIES` shows aggregate
 * pressure; this attributes per-inode.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified against include/trace/events/writeback.h
 * on 6.17 — note `writeback_dirty_page` was renamed to
 * `writeback_dirty_folio` in the folio refactor):
 *   - tracepoint/writeback/writeback_dirty_inode    — inode marked dirty
 *   - tracepoint/writeback/writeback_dirty_folio    — folio dirtied
 *   - tracepoint/writeback/writeback_pages_written  — N pages flushed
 *   - tracepoint/writeback/writeback_written        — wb chunk done
 *   - tracepoint/writeback/writeback_single_inode   — per-inode flush
 *   - tracepoint/writeback/balance_dirty_pages      — already in SenseHub
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_inode: HASH[inode_no → {dirty_count, written_count,
 *                                first_dirty_ts, last_written_ts}]
 *              LRU_HASH, max 4096
 * - per_bdi:   HASH[bdi_id → {pages_dirtied, pages_written,
 *                              throttle_ns}]
 * - timeline:  ARRAY[1] + BPF_F_MMAPABLE — recent {inode_no,
 *              kind, page_count, ts_ns}
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns.  Event rate: 100-100K events/sec depending
 * on dirty-page rate.  Page-level events (`writeback_dirty_page`)
 * are highest rate; sample-period gating recommended for that one.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - inode_no → file path resolution is expensive; do at userspace
 *   via `iter_task` walk (sibling) finding the open fd by inode,
 *   OR by `find /proc/*/fd -inum N` scan at bench-end.
 * - Per-mount aggregation requires bdi (backing device info)
 *   association; LRU_HASH on bdi_id covers that.
 * - bcachefs / fuse / overlayfs all funnel through these
 *   tracepoints; per-FS-type breakdown via inode->sb_id (added
 *   field via BPF_CORE_READ if available).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SenseHub `WRITE_THROTTLE_JIFFIES` (aggregate)
 * Sibling: vfs_hot.bpf.c (entry-side write attribution)
 * Sibling: iter_task.bpf.c (for inode→path resolution)
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
