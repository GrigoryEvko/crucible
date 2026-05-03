/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * nvme_rq.bpf.c — Per-NVMe-command setup→complete latency, per-queue per-opcode.
 *
 * STATUS: doc-only stub.  Tier-E.  For Cipher cold-tier I/O diagnostics.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Block layer's `block_rq_*` tracepoints aggregate at the request layer.
 * For NVMe-direct paths (Cipher's cold tier), per-NSID, per-queue, and
 * per-opcode latency tells you "queue 7 of NSID 1 is hot" or "the SQE
 * fence is stalling completion" — concrete diagnostics for the
 * fast-storage tier.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/nvme/nvme_setup_cmd      — host built the SQE
 *   - tracepoint/nvme/nvme_complete_rq    — host saw the CQE
 *   - tracepoint/nvme/nvme_sq             — submission queue doorbell
 *     written
 *   - tracepoint/nvme/nvme_async_event    — out-of-band controller event
 * Kernel min: 5.0.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - rq_inflight: HASH[(ctrl_id, qid, cmdid) → setup_ts]
 *                LRU_HASH, max 32K — Cipher can have ~10K in-flight
 * - lat_by_op:   HASH[(ctrl_id, opcode) → log-bucket histogram]
 *                opcodes: READ, WRITE, FLUSH, DSM (deallocate),
 *                COMPARE, WRITE_ZEROES, ZONE_APPEND, etc.
 * - lat_by_qid:  PERCPU_HASH[(ctrl_id, qid) → {count, total_ns, max_ns}]
 *                per-queue-per-CPU pressure (queues are CPU-pinned)
 * - timeline:    ARRAY[1] + BPF_F_MMAPABLE — recent {ctrl_id, qid,
 *                opcode, lat_ns, ts_ns} for tail-latency outlier digs
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~100-200 ns (2 tracepoints × ~50-100 ns + map churn).
 * Event rate: Cipher cold tier ~10K-100K IOPS sustained = ~30K-300K
 *   tracepoint fires/sec.
 * Per-sec overhead: ~0.3-3% — opt-in via env var
 *   `CRUCIBLE_PERF_NVME_RQ=1`; off by default outside of Cipher diag runs.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: NvmePmu (controller-side counters, vendor SDK).
 * Sibling: BlockRq (block-layer aggregate; this is NVMe-specific).
 * Aggregates with: SenseHub `DISK_IO_*` (block-layer aggregate).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
