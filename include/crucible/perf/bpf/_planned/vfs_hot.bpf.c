/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * vfs_hot.bpf.c — fentry on vfs_read / vfs_write / vfs_open.
 *
 * STATUS: doc-only stub.  Tier-H.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * VFS-layer attribution: which file was hot for read/write, what was
 * the per-call latency, which fd is contended.  Cipher cold-tier
 * + log-write paths are the typical consumers.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: fentry/fexit (kernel ≥ 5.5)
 * Attachment points:
 *   - fentry/vfs_read              — read syscall path
 *   - fexit/vfs_read               — exit, get retval
 *   - fentry/vfs_write             — write syscall path
 *   - fexit/vfs_write
 *   - fentry/do_filp_open          — file open
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - in_flight: HASH[(tid) → (op, start_ts, file_inode)] — per-thread
 *              in-flight VFS op
 * - per_inode_lat: LRU_HASH[inode → {read_ns, write_ns, count}]
 * - per_op_hist: ARRAY[16] — log-bucket latency histogram per op
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~80 ns (fentry + fexit pair).
 * Event rate: 100K-10M VFS ops/sec on busy hosts.
 * Per-sec overhead: 0.8-80% — UNACCEPTABLE without filter.  Default
 *   off; opt-in env var, with 1-in-N sampling.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - is_target() filter mandatory — kernel-wide vfs_read fires for
 *   every process.
 * - inode resolution to path requires d_path() which is bounded but
 *   expensive; do at userspace using inode → /proc/PID/fd lookup.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SenseHub `IO_*` (syscall-level aggregate).
 * Sibling: BlockRq (block-layer attribution; this is VFS layer).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
