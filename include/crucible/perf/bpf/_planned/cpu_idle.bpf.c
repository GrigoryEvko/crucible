/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * cpu_idle.bpf.c — Per-CPU idle state residency.
 *
 * STATUS: doc-only stub.  Tier-H.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * Deep idle states (C6, C7, package-C-states) save power but cost
 * tens of µs to wake from.  For latency-bench code, "CPU went into
 * C6 between iterations" → next iteration starts ~30 µs late.
 * SenseHub doesn't break out idle state residency; this exposes it.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/power/cpu_idle              — entered/exited idle state
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_cpu_state: PERCPU_ARRAY[N_C_STATES=8] — residency_ns per state
 * - state_changes: PERCPU_ARRAY[1] — count of idle entries
 * - last_state:    PERCPU_ARRAY[1] — current state + entry timestamp
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: ~1K-100K idle transitions/sec/CPU.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Aggregates with: SenseHub `CPU_FREQ_CHANGES`.
 * Aggregates with: SchedSwitch (preemption back to runnable triggers
 *   idle exit).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
