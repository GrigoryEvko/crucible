/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * rwsem_rtmutex.bpf.c — rwsem + rtmutex specific contention.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-2 addition.  Sibling
 * to existing lock_contention.bpf.c (which is generic).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Generic lock_contention captures spinlocks + mutexes by IP.  Doesn't
 * surface:
 *   - rwsem reader-vs-writer-lock-specific patterns: writer starvation
 *     (many readers; one writer waits forever), reader-on-write-stall.
 *   - rtmutex priority inheritance events: who got boosted, by how much,
 *     for how long.  Critical for SCHED_FIFO/SCHED_RR realtime workloads.
 *   - mmap_lock differs from generic rwsem (own probe set, mmap_lock.bpf.c).
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: kprobe/kretprobe + tracepoint
 * Attachment points:
 *   - kprobe/down_read / down_write / up_read / up_write
 *   - kprobe/rt_mutex_lock / rt_mutex_unlock
 *   - tracepoint/lock/contention_begin / contention_end (variant info)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - rwsem_writer_starve: HASH[lock_addr → max_wait_ns]
 * - rt_boost_events: LRU_HASH[(boostee_tid, booster_tid) → boost_ns]
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100 ns (kprobe overhead).  Event rate: 1K-100K/sec
 * on busy host.  Per-sec: 0.01-1%.  Default-off; opt-in via
 * `CRUCIBLE_PERF_RWSEM=1`.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - kprobe attach to rwsem helpers depends on inlining decisions;
 *   add fentry alternative when available.
 * - rtmutex usage on non-RT kernels is sparse; mainly meaningful when
 *   PREEMPT_RT or SCHED_FIFO threads in play.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: lock_contention.bpf.c (existing, generic).
 * Sibling: mmap_lock.bpf.c (planned, mmap_lock-specific).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
