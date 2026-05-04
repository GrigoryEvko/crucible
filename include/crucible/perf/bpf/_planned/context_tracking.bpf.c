/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * context_tracking.bpf.c — kernel↔user mode transition observation.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Verified
 * against /sys/kernel/tracing/events/context_tracking/ on 6.17 (2 events).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Every syscall is a user→kernel transition (`user_exit` from RCU's
 * perspective) and a kernel→user transition on return (`user_enter`).
 * On RCU-NOHZ_FULL CPUs (kernel boot param `nohz_full=N`) this
 * transition is also where RCU enters extended quiescent state —
 * load-bearing for tickless real-time benchmarks.
 *
 * Per-transition counts answer:
 *   - "How many syscalls per second is our task making?"  (free; cheaper
 *     than per-syscall syscall_latency.bpf.c)
 *   - "Is RCU correctly noticing our task is in userspace?"  (real-time
 *     correctness check)
 *   - On NOHZ_FULL: do we have any unnecessary kernel re-entries that
 *     defeat the tickless-CPU optimization?
 *
 * For Crucible bench harness: a sudden spike in user_exit count
 * indicates the workload started syscalling more — usually a sign
 * something fell off the fast path (e.g. malloc moved to a slow
 * path that traps to mmap).
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17 — 2 events):
 *   - tracepoint/context_tracking/user_enter   — return to userspace
 *   - tracepoint/context_tracking/user_exit    — entry into kernel
 *                                                 (most syscalls fire this)
 *
 * Per-event payload includes the prev/next state (USER, KERNEL,
 * IDLE, GUEST).  CPU index from bpf_get_smp_processor_id().
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_cpu_count: PERCPU_ARRAY[2] — slot[0] = user_enter count,
 *                                     slot[1] = user_exit count
 * - per_pid_summary: HASH[tid → {transitions_per_window, total_kernel_ns}]
 *                    LRU max 4096
 * - cpu_in_kernel_ns: PERCPU_ARRAY[1] — accumulated kernel-mode time
 *                                        per CPU (entry/exit timestamp diff)
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * No timeline (rate too high on busy hosts — 100K-1M transitions/sec).
 * Histogram + counter snapshot per bench window.
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns × 100K-1M events/sec = 0.5%-50% CPU.  **TOO HIGH
 * for always-on**.  Default-off; opt-in via `CRUCIBLE_PERF_CTX_TRACK=1`.
 *
 * Useful sub-modes via env var:
 *   - `CRUCIBLE_PERF_CTX_TRACK=1` — full per-transition (heavy)
 *   - `CRUCIBLE_PERF_CTX_TRACK=count` — counter-only, no kernel-time
 *     measurement (lighter — counts only)
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Tracepoints only fire when CONFIG_CONTEXT_TRACKING_USER=y (default-y
 *   in modern distros for NOHZ_FULL support; verify before deployment).
 * - Doesn't distinguish syscalls from interrupts (both go through
 *   user_exit on RCU's view).  For syscall-only attribution use
 *   syscall_latency.bpf.c (existing program).
 * - On non-NOHZ_FULL CPUs the events fire less frequently (only on
 *   actual user/kernel boundary — RCU reports periodic ticks not
 *   user_enter/exit).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: syscall_latency.bpf.c (existing) — per-syscall timing;
 *   superset signal for syscall-only attribution.
 * Sibling: rcu_gp.bpf.c (planned) — RCU grace-period timing;
 *   user_enter/exit are RCU's view of "task in userspace, safe to
 *   advance grace period".
 * Aggregates with: SchedSwitch — every sched_switch is preceded by
 *   user_exit on the outgoing CPU.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
