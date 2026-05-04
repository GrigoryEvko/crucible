/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * filemap.bpf.c — page cache lookup / fault / map attribution.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Verified
 * against /sys/kernel/tracing/events/filemap/ on 6.17 (5 events).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Page cache is the largest unobserved shared resource on most
 * Crucible deployments.  Cipher cold-tier reads, model-weight mmaps,
 * Vessel-loaded shared libraries, third-party tenant I/O — all share
 * the same kernel page cache and contend for the same eviction policy.
 *
 * Per-file attribution answers:
 *   - Which file is being page-faulted into right now?  (cold-load detection)
 *   - Which file is being evicted?  (memory-pressure attribution)
 *   - Which file's mappings are being walked?  (mmap'd-file scan detection)
 *
 * Bench-relevance: a "cold" iteration that faults in 100 MB of weights
 * looks like generic memory bandwidth pressure in PmuSample;
 * filemap fires WHICH file caused it.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17 — 5 events):
 *   - tracepoint/filemap/mm_filemap_add_to_page_cache
 *                                    — page added (read populated cache)
 *   - tracepoint/filemap/mm_filemap_delete_from_page_cache
 *                                    — page evicted
 *   - tracepoint/filemap/mm_filemap_fault   — page-fault on file mapping
 *   - tracepoint/filemap/mm_filemap_get_pages — pages returned to caller
 *   - tracepoint/filemap/mm_filemap_map_pages — VMA fault-in batch
 *
 * Per-event payload includes inode_id, page index (offset/PAGE_SIZE),
 * mapping flags.  inode→file path resolution requires userspace
 * `iter_task` or `find /proc/*/fd -inum` walk at bench-end.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_inode: LRU_HASH[inode_id → {add_count, delete_count, fault_count,
 *                                    last_access_ts, byte_count}]
 *              LRU max 4096 (working set of hot files)
 * - rate_limit: PERCPU_ARRAY[1] — sample-period gating (1-in-N events)
 *               recommended to throttle add/delete which fire at ~1M/sec
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — recent {inode, idx, kind, ts}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_filemap_event {
 *     uint64_t inode_id;
 *     uint64_t page_idx;
 *     uint8_t  event_kind;       // ADD / DELETE / FAULT / GET / MAP
 *     uint8_t  _pad[7];
 *     uint64_t ts_ns;            // WRITTEN LAST
 * };  // 32 B (cache-line coresident)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns × 1M events/sec on busy I/O host = 50% CPU.
 * **TOO HIGH for always-on**.  Default-off; opt-in via
 * `CRUCIBLE_PERF_FILEMAP=1` for cold-tier diagnostic runs.
 *
 * Sample-period gating MANDATORY: 1-in-1024 events for distribution
 * stats (drops cost to 0.05% CPU) — set via map config.
 *
 * mm_filemap_fault is rarer (page faults only, not every cache hit) —
 * default-on safe at <0.01% CPU.  Other 4 events default-off.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - inode→path resolution is expensive at runtime; do at userspace via
 *   `iter_task` walk at bench-end.
 * - Doesn't distinguish Crucible's own file traffic from other tenants;
 *   for per-tenant attribution combine with cgroup_id from
 *   bpf_get_current_cgroup_id().
 * - shmem and tmpfs go through filemap — high-rate even on
 *   "no I/O" benches that touch /tmp.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: writeback_inode.bpf.c (per-inode writeback) — pairs with
 *   this for read+write attribution per file.
 * Sibling: vfs_hot.bpf.c (planned) — VFS-layer entry for read/write/open;
 *   filemap is the page-cache layer beneath VFS.
 * Sibling: vmscan_ext.bpf.c (planned) — page eviction reasons; filemap
 *   sees the eviction (delete_from_page_cache) but not the WHY.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
