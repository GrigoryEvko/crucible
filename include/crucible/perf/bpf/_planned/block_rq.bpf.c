/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * block_rq.bpf.c — block_rq_* lifecycle (insert→issue→complete).
 *
 * STATUS: doc-only stub.  Tier-E.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * SenseHub aggregates bio-level (block_io_start/done).  block_rq_*
 * exposes the I/O scheduler stages: insert (queued in IO sched) →
 * issue (sent to driver) → complete (driver responded).  Per-stage
 * latency tells you whether the I/O scheduler (mq-deadline / kyber /
 * bfq / none) is the offender vs the device itself.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/block/block_rq_insert    — request queued in IO sched
 *   - tracepoint/block/block_rq_issue     — request sent to driver
 *   - tracepoint/block/block_rq_complete  — driver responded
 *   - tracepoint/block/block_rq_requeue   — driver kicked back
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - rq_state: HASH[(dev, sector, bytes) → {insert_ts, issue_ts}]
 *             LRU_HASH, max 16K
 * - lat_per_stage: ARRAY[3] — {queue_lat_ns, exec_lat_ns, complete_lat_ns}
 *                  aggregated across all in-flight (queue=insert→issue,
 *                  exec=issue→complete)
 * - lat_per_io_sched: HASH[io_sched_name[16] → {q_count, total_ns}]
 *                     identify which scheduler is dominating queue time
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~80-150 ns (3 tracepoints).
 * Event rate: same as block_io_start rate (typically lower than NVMe
 *   raw rate — block layer aggregates).  ~1K-100K req/sec.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: NvmeRq (NVMe-specific, more granular).
 * Sibling: SenseHub `DISK_IO_*` (bio aggregate).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
