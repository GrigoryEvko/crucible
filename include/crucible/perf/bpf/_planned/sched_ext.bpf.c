/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sched_ext.bpf.c — Crucible-aware kernel scheduler via struct_ops/sched_ext.
 *
 * STATUS: doc-only stub.  Tier-D.  Kernel 6.12+ required.
 * Heaviest implementation in the planned set — true kernel-side
 * scheduler that REPLACES CFS for our task class.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * CFS is fairness-optimized; bench wants tail-latency-optimized.
 * "Pin our worker thread to CPU N, quiesce non-essential interferers
 * on N during the bench window, restore at the end" requires either
 * (a) cooperating with CFS via cpusets + nice (poor isolation), or
 * (b) shipping a Crucible-aware scheduler in BPF.  sched_ext (kernel
 * 6.12 LTS) makes (b) tractable.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: struct_ops/sched_ext (BPF program implements
 * `struct sched_ext_ops` — kernel calls our enqueue/dequeue/dispatch
 * callbacks instead of CFS's).
 *
 * Hooks we implement:
 *   - .enqueue   — task became runnable, decide which DSQ to put it on
 *   - .dispatch  — pick next task from DSQs
 *   - .running   — task started running (record start ts)
 *   - .stopping  — task stopped (record runtime)
 *   - .runnable  — task transitioned to runnable
 *   - .quiescent — task transitioned to non-runnable
 *
 * Additionally:
 *   - .init      — scheduler attached
 *   - .exit      — scheduler detached
 *   - .select_cpu — CPU selection for waking task
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - bench_pid:    ARRAY[1] — our bench worker TID (set from userspace)
 * - bench_cpu:    ARRAY[1] — preferred CPU for the bench worker
 * - quiesce_set:  HASH[tid → bool] — TIDs to deprioritize during bench
 * - dsq_id_per_cpu: PERCPU_ARRAY[1] — local DSQ identifier
 * - sched_decisions: ARRAY[1] + BPF_F_MMAPABLE — per-decision telemetry
 *                   (which CPU picked, why, alternatives considered)
 *
 * ─── POLICY ──────────────────────────────────────────────────────────
 * 1. Bench worker TID always dispatched to bench_cpu DSQ first.
 * 2. Quiesce-set TIDs given LIFO ordering on a low-priority DSQ;
 *    they only run when bench DSQ is empty.
 * 3. Other tasks: vanilla CFS-like fair share via global DSQ.
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-context-switch: ~200-500 ns (BPF callback dispatch + map
 * lookups vs CFS's ~50-100 ns).  Acceptable on bench cores; concerning
 * on infrastructure cores — gate via cpuset to only run sched_ext on
 * the bench CPUs.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Requires CONFIG_SCHED_CLASS_EXT=y.  Enablement is via attaching
 *   a struct_ops scheduler (no sysctl); kernel exposes status via
 *   /sys/kernel/sched_ext/state and the static key __scx_enabled.
 * - sched_ext API is still stabilizing (6.12+); minor breakage
 *   between 6.12 and 6.18 expected.  Pin to a kernel range in the
 *   facade and refuse to load on out-of-range kernels.
 * - Detaching the scheduler returns the kernel to CFS — verify this
 *   on shutdown to avoid leaving the system on a stale BPF scheduler.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SchedSwitch (post-ext sched_switch events still fire,
 *   carry our sched_ext-decided next_pid).
 * Aggregates with: SchedRq (run-queue depth visible to sched_ext too).
 * Inverse-of: nothing — this REPLACES CFS for our task class.
 */

#include "../common.h"
#include <bpf/bpf_helpers.h>

/* TODO: implement.  Reference: tools/sched_ext/scx_simple.bpf.c in the
 * Linux source tree (uses scx/common.bpf.h, UEI_DEFINE, SCX_DSQ_GLOBAL).
 * Implement struct sched_ext_ops with our policy. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
