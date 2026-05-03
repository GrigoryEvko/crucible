/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * slab_allocator.bpf.c — kmem/kmem_cache_alloc per cache rate + size class.
 *
 * STATUS: doc-only stub.  Tier-C.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SLAB/SLUB allocator churn dominates kernel-side pain in I/O-heavy
 * code.  Per-cache attribution (kmalloc-128 vs task_struct vs sock
 * vs dentry) tells you "WHICH allocator family is the offender" —
 * dentry/inode pressure is filesystem cache thrash, sock pressure is
 * connection churn, kmalloc-128 explosion is some kernel module
 * leaking small allocations.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/kmem/kmem_cache_alloc       — slab object alloc'd
 *   - tracepoint/kmem/kmem_cache_alloc_node  — same, NUMA-explicit
 *   - tracepoint/kmem/kmem_cache_free        — returned
 *   - tracepoint/kmem/kfree                  — generic kfree
 *   - tracepoint/kmem/kmalloc                — kmalloc
 *   - tracepoint/kmem/kmalloc_node           — kmalloc NUMA-explicit
 * Kernel min: 4.0; SLUB-only path on modern kernels (SLAB removed in 6.5).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - cache_stats: HASH[(cache_struct *) → {alloc_count, free_count,
 *                                          peak_inflight, total_bytes}]
 *                LRU_HASH, max_entries 4096 — slab cache count
 * - cache_names: HASH[(cache_struct *) → char[32]]  — name string
 *                cache (filled at first sight via BPF_CORE_READ_STR)
 * - kmalloc_size_hist: ARRAY[16] — log-bucketed size histogram
 *                                  for the kmalloc/kfree path
 * - per_node:    PERCPU_ARRAY[N_NUMA_NODES] — per-NUMA alloc count
 *                (detect cross-node slab traffic)
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * No timeline (rate way too high).  Userspace reads aggregated maps.
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns.
 * Event rate: 100K-10M slab ops/sec — VERY high on busy hosts.
 * Per-sec overhead: 5-50% naive — UNACCEPTABLE.  Default OFF; opt-in
 * via env var `CRUCIBLE_PERF_SLAB=1` and even then rate-limited to
 * 1-in-256 sampling.  Allocations are the kind of thing you instrument
 * for one diagnosis run, not always-on.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Cache-name string read in BPF requires bounded-size buffer
 *   (max_entries * 32 B); per-cache strings are stable so first-seen
 *   lookup is fine.
 * - Userspace can also `cat /proc/slabinfo` for per-cache snapshot —
 *   simpler and equally cheap if you don't need per-event timing.
 *   This BPF is for (a) call-site attribution via stack_id and
 *   (b) per-allocation latency under contention.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Promoted from: SenseHub `SLAB_ALLOCS` / `SLAB_FREES` (aggregate;
 *   no per-cache breakdown).
 * Aggregates with: page_allocator (slab caches refill from page
 *   allocator on growth).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
