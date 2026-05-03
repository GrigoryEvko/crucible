/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * iter_mmap.bpf.c — Walk every mmap region (task_vma).
 *
 * STATUS: doc-only stub.  Tier-G.  BPF iter ITER_TYPE_TASK_VMA
 * (kernel 5.13+).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Per-task VMA inventory: which file backs each mapping, size, prot,
 * usage flags.  Replaces /proc/PID/maps walk.  Useful for "huge-page
 * accounting per region" and "what JIT code did the runtime emit".
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: BPF_TRACE_ITER ITER_TYPE_TASK_VMA
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct mmap_iter_record {
 *     __u32 tgid;
 *     __u32 _pad;
 *     __u64 start, end;        // VMA range
 *     __u64 flags;             // VM_READ/WRITE/EXEC/SHARED/HUGEPAGE/...
 *     __u64 pgoff;             // file offset
 *     char  file[64];          // file basename (or [anon], [stack])
 * };
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-VMA: ~200 ns (inc. file path resolve).  N_VMAs typically
 * 100-10K → ~1 ms total per process.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: MmapLock (lock contention on the mm).
 * Sibling: PageAllocator (per-VMA allocation attribution).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
