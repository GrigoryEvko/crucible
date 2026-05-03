/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * mmap_lock.bpf.c — mmap_lock contention timeline (per-mm, per-op).
 *
 * STATUS: doc-only stub.  Tier-C.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * The per-mm `mmap_sem` (kernel ≥ 5.8: `mmap_lock`) protects the
 * VMA tree.  ANY page-fault, mmap, munmap, mremap, mprotect on the
 * SAME mm acquires it.  In multi-threaded compute one thread doing
 * mmap can stall all the others' page-faults for hundreds of µs.
 * This is THE classic "weird outcome" cause for tail-latency spikes
 * in multi-threaded ML pipelines.
 *
 * SenseHub gets a count via the cheap-tracepoint extension.  This
 * standalone facade adds per-(mm, op) attribution + wait-time
 * timeline.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (kernel 5.16+):
 *   - tracepoint/mmap_lock/mmap_lock_start_locking — try to acquire
 *   - tracepoint/mmap_lock/mmap_lock_acquire_returned — got it (ts)
 *   - tracepoint/mmap_lock/mmap_lock_released — drop
 * Args carry: mm pointer, write/read mode, success/fail.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - acq_pending: HASH[(mm, tid) → start_ts]  — in-flight acquires
 *                LRU_HASH; orphans (failed acquires never reporting
 *                released) auto-evict
 * - mm_stats:    HASH[mm → {wait_ns, count, max_wait_ns,
 *                writer_count, reader_count}]
 * - timeline:    ARRAY[1] + BPF_F_MMAPABLE — recent {mm, tid, mode,
 *                wait_ns, ts_ns} for "which mmap stalled us"
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_mmap_lock_event {
 *     __u64 mm_id;          // hash of mm_struct addr
 *     __u64 wait_ns;        // start_locking → acquire_returned
 *     __u32 tid;
 *     __u8  mode;           // 0=read, 1=write
 *     __u8  success;        // ksym mapped to bool
 *     __u8  _pad[2];
 *     __u64 ts_ns;          // WRITTEN LAST
 *     __u64 _pad8;
 * };  // 32 B (cache-line coresident)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100 ns (3 tracepoints × ~30-50 ns).
 * Event rate: ~10-1000 acquires/sec/process baseline; spikes to
 * millions/sec under fork/page-fault storms.  Gate per-event timeline
 * emission behind a "wait_ns > 1µs" filter to avoid spamming the
 * ring with cheap (uncontended) acquires.
 * Per-sec overhead: ~0.01% normal, up to 0.5% under storm (filter on).
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Tracepoints kernel 5.16+ — older kernels won't have these and
 *   the facade returns nullopt at load() (graceful degradation).
 * - Per-VMA contention requires walking the VMA tree at userspace
 *   read time (hint via `mmap_iter` BPF iter, sibling stub
 *   `iter_mmap.bpf.c`).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SchedSwitch (long mmap_lock wait → off-CPU).
 * Aggregates with: SenseHub `MMAP_LOCK_WAITS` (count + total_ns).
 * Promotes from: SenseHub (per-(mm,op) needs hash, can't fit Idx slot).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
