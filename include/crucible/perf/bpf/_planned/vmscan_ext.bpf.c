/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * vmscan_ext.bpf.c — Extended vmscan: kswapd wake/sleep, lru_isolate, reclaim path.
 *
 * STATUS: doc-only stub.  Tier-C.  Sibling to existing
 * `DIRECT_RECLAIM_*` counters in SenseHub (synchronous reclaim).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SenseHub tracks direct reclaim (sync, our task pays).  But kswapd
 * (async, kernel pays but steals CPU + memory bandwidth) is invisible.
 * Memory-pressure-induced bench tail-latency typically comes from
 * kswapd, not direct reclaim — kswapd wakes in advance of pressure
 * to keep watermarks healthy, and on a memory-bound workload it runs
 * continuously, stealing 5-30% of the bandwidth our compute wanted.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified against /sys/kernel/tracing/events/vmscan
 * on 6.17 — note `mm_vmscan_writepage` was removed in the folio
 * refactor):
 *   - tracepoint/vmscan/mm_vmscan_kswapd_wake
 *   - tracepoint/vmscan/mm_vmscan_kswapd_sleep
 *   - tracepoint/vmscan/mm_vmscan_wakeup_kswapd     — direct wake
 *   - tracepoint/vmscan/mm_vmscan_lru_isolate       — pages isolated
 *   - tracepoint/vmscan/mm_vmscan_lru_shrink_active
 *   - tracepoint/vmscan/mm_vmscan_lru_shrink_inactive
 *   - tracepoint/vmscan/mm_shrink_slab_start / _end — shrinker invocation
 * Kernel min: 4.0 (kswapd/lru_*); shrink_slab_* since 4.16.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - kswapd_state: PERCPU_ARRAY[1] — current kswapd CPU's run start ts
 * - kswapd_stats: ARRAY[1] — {total_run_ns, wake_count, pages_isolated,
 *                  pages_reclaimed_active, pages_reclaimed_inactive}
 * - reclaim_path_breakdown: ARRAY[N_PATHS] — per reclaim entry-point
 *                  bucket (alloc_pages, balance_dirty_pages, etc.)
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * No timeline (kswapd is steady-state); just counters in a `Snapshot`-
 * shaped struct.  Userspace reads via mmap'd map.
 *
 * struct vmscan_snapshot {
 *     __u64 kswapd_run_ns;
 *     __u64 kswapd_wakes;
 *     __u64 pages_isolated;
 *     __u64 pages_shrunk_active;
 *     __u64 pages_shrunk_inactive;
 *     __u64 _reserved[3];
 * };  // 64 B = 1 cache line
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns each.
 * Event rate: ~10-100 wakes/sec normal; up to 1000/sec under pressure.
 * lru_isolate fires per-batch (1 event for 32 pages), so even at
 * 10K isolated pages/sec the tracepoint rate stays bounded.
 * Per-sec overhead: ~0.001-0.01%.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - System-wide; no PID filter (kswapd is per-NUMA-node kernel thread,
 *   not bound to our tgid).  Reflects total system memory pressure
 *   our workload contributes to.
 * - kswapd_run_ns measured per-CPU; aggregate across nodes for total.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SenseHub `DIRECT_RECLAIM_*` (sync), `SWAP_OUT_PAGES`,
 *   `WRITE_THROTTLE_JIFFIES`.
 * Aggregates with: PmuUncoreImc (memory bandwidth — kswapd steals it).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
