/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * iter_task.bpf.c — Walk every task in process for periodic snapshots.
 *
 * STATUS: doc-only stub.  Tier-G.  BPF iter (kernel 5.8+).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Periodic process-state snapshot — RSS per thread, CPU usage per
 * thread, comm, state, cgroup_id — without paying per-event cost.
 * BPF iter walks `for_each_task` from kernel space; userspace gets
 * a structured stream.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: BPF_PROG_TYPE_TRACING / BPF_TRACE_ITER
 * Attach: `bpf_link__create(prog, ITER_TYPE_TASK, ...)`
 * Triggered: on read() from userspace iter FD; emits via
 *   `bpf_seq_write` to the userspace reader.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * No persistent maps — output is the iter stream itself.  Userspace
 * ring-buffers the emitted records.
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct task_iter_record {
 *     __u32 tid, tgid, ppid;
 *     __u32 cpu_last;          // task->wake_cpu
 *     __u64 utime_ns, stime_ns;
 *     __u64 rss_anon, rss_file;
 *     __u64 cgroup_id;
 *     char  comm[16];
 *     __u32 state;             // TASK_RUNNING/INTERRUPTIBLE/etc.
 * };  // emit per task; ~80 B
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-iteration: ~100 ns × N_tasks.  For N=10K tasks, ~1 ms total
 * to walk the system.  Run periodically (1 Hz) → ~0.1% CPU.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - No transactional snapshot; tasks can fork/exit during walk.
 *   Document this — Augur uses it as approximation, not census.
 * - bpf_iter requires kernel 5.8+; falls back to /proc walk on older
 *   kernels (much slower, but available everywhere).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SocketIter, MmapIter, CgroupIter — composable batch.
 * Aggregates with: PsiReader (system pressure).
 */

#include "../common.h"

/* TODO: implement.  Reference: kernel selftests/bpf/progs/bpf_iter_task.c. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
