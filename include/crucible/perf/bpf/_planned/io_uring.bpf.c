/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * io_uring.bpf.c — io_uring submission/completion attribution.
 *
 * STATUS: doc-only stub.  Tier-1 audit-round-2 addition.  Eventual
 * path: `include/crucible/perf/bpf/io_uring.bpf.c` + facade.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Modern Linux high-perf I/O is converging on io_uring (kernel 5.1+,
 * mature 5.10+).  Sits ABOVE NVMe / block / sockets and aggregates
 * everything: file I/O, network sendmsg/recvmsg, accept, splice,
 * timeouts, futex.  Cipher cold-tier and CNTP both likely consume
 * io_uring.  Per-opcode latency, completion-ring overflow, link-chain
 * stall attribution are invisible without these tracepoints.
 *
 * Bigger gap than nvme_rq.bpf.c — io_uring is the modern I/O substrate
 * and aggregates everything below it.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint (extensive io_uring tracepoint set,
 * kernel 5.10+; richer set in 6.x)
 * Attachment points (~12 tracepoints in modern kernel):
 *   - tracepoint/io_uring/io_uring_create        — ring created
 *   - tracepoint/io_uring/io_uring_register      — fixed file/buffer registered
 *   - tracepoint/io_uring/io_uring_submit_req    — SQE submitted
 *   - tracepoint/io_uring/io_uring_queue_async_work — punted to async worker
 *   - tracepoint/io_uring/io_uring_defer         — deferred for ordering
 *   - tracepoint/io_uring/io_uring_link          — linked SQE chain
 *   - tracepoint/io_uring/io_uring_cqring_wait   — waiter blocked on CQ
 *   - tracepoint/io_uring/io_uring_complete      — CQE posted
 *   - tracepoint/io_uring/io_uring_cqe_overflow  — CQ ring overflowed
 *   - tracepoint/io_uring/io_uring_task_add      — task_work scheduled
 *   - tracepoint/io_uring/io_uring_short_write   — short write
 *   - tracepoint/io_uring/io_uring_local_work_run — local-work executed
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - inflight: HASH[(ring_fd, user_data) → submit_ts] — per-SQE timing
 *             LRU_HASH, max 65536
 * - lat_per_op: HASH[opcode → log-bucket histogram] — per-opcode
 *               (READ, WRITE, SENDMSG, RECVMSG, ACCEPT, CONNECT,
 *                FSYNC, SPLICE, FALLOCATE, MADVISE, OPENAT,
 *                STATX, FADVISE, EPOLL_CTL, TIMEOUT, ~50 ops total)
 * - cq_overflow: ARRAY[1] — total CQ overflow count
 * - link_stalls: HASH[chain_id → stall_ns] — links blocked by
 *                preceding SQE
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — recent {ring_fd, opcode,
 *             lat_ns, ts_ns} for outlier diagnosis
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_iouring_event {
 *     __u64 user_data;       // app-supplied tag for the SQE
 *     __u64 lat_ns;          // submit → complete
 *     __u32 ring_fd;
 *     __u8  opcode;          // io_uring opcode (IORING_OP_*)
 *     __u8  flags;           // IOSQE_* flags
 *     __u8  _pad[2];
 *     __u64 ts_ns;           // WRITTEN LAST
 * };  // 32 B (cache-line coresident)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns (one map_lookup + one timestamp + atomic).
 * Event rate: depends — Cipher cold tier could be 100K-1M IOPS, CNTP
 * could be 10K-100K msgs/sec, both via io_uring.  Per-sec overhead at
 * 1M ops/sec × 100 ns = 10% — too high.
 *
 * Mitigation: filter by ring_fd via userspace-populated allow-list
 * (only OUR rings), OR sample 1-in-N events.  Default off; opt-in via
 * `CRUCIBLE_PERF_IOURING=1`.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Tracepoint set has grown across kernels (5.10 minimal, 6.6+ rich).
 *   Per-version availability check at load.
 * - SQ-poll mode (IORING_SETUP_SQPOLL) bypasses some tracepoints
 *   because the kernel polls the SQ from a kernel thread instead of
 *   userspace syscalling — coverage gap.
 * - Linked-SQE timing is approximate when the chain sees errors mid-
 *   way (kernel cancels remainder; tracepoint sees individual cancels).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sits above: nvme_rq.bpf.c (per-NVMe-cmd), block_rq.bpf.c (block
 *   layer), sock_ops.bpf.c (TCP), netif_receive.bpf.c (RX) — io_uring
 *   submissions land in any of these.  Cross-correlate via timestamp.
 * Replaces (for io_uring users): per-opcode tracking via syscall
 *   tracepoints (which io_uring bypasses).
 */

#include "../common.h"

/* TODO: implement.  Reference: bcc/tools/iourings.py for the
 * userspace pattern; Jens Axboe's liburing source for opcode
 * enumeration; tools/io_uring/ in kernel for the canonical
 * tracepoint usage. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
