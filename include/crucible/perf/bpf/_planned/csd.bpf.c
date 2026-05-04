/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * csd.bpf.c — Cross-CPU smp_call_function_single latency.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Verified
 * against /sys/kernel/tracing/events/csd/ on kernel 6.17.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * `smp_call_function_single` (csd = call-single-data) lets a CPU
 * synchronously invoke a function on another CPU.  Used heavily for:
 *   - TLB-flush coordination (also captured via TLB_SHOOTDOWNS in SenseHub-ext)
 *   - Per-CPU data structure management
 *   - Frequency / power state coordination
 *   - Atomic global-state mutation requiring all-CPU serialization
 *
 * Different from the IPI facade (`ipi_events.bpf.c`) which captures
 * raw APIC IPI delivery — csd is the higher-level "schedule N to run
 * a function on remote CPU and wait" abstraction.  Crucible cares
 * because cross-CPU function calls during bench windows look like
 * "preemption" in SchedSwitch but are actually kernel-driven IPI
 * cascades.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17 — 3 events):
 *   - tracepoint/csd/csd_queue_cpu        — caller queued work for remote CPU
 *   - tracepoint/csd/csd_function_entry   — remote handler started
 *   - tracepoint/csd/csd_function_exit    — remote handler returned
 *
 * Per-pair latency = csd_function_entry.ts - csd_queue_cpu.ts (queue→start)
 * Per-handler duration = csd_function_exit.ts - csd_function_entry.ts
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - inflight: PERCPU_HASH[(src_cpu, dst_cpu, csd_addr) → queue_ts]
 *             LRU_HASH max 4096 (multi-CPU concurrent csd traffic)
 * - per_pair_lat: HASH[(src_cpu, dst_cpu) → log-bucket histogram]
 * - per_func: HASH[func_addr → {call_count, total_ns, max_ns}]
 *             function pointer is `csd->func`; userspace resolves via /proc/kallsyms
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — recent {src_cpu, dst_cpu,
 *             func_addr, queue_to_start_ns, handler_ns, ts_ns}
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_csd_event {
 *     uint64_t func_addr;            // resolves via kallsyms
 *     uint64_t queue_to_start_ns;    // queue → handler entry
 *     uint64_t handler_ns;           // entry → exit
 *     uint16_t src_cpu;
 *     uint16_t dst_cpu;
 *     uint8_t  _pad[4];
 *     uint64_t ts_ns;                // WRITTEN LAST
 * };  // 32 B (cache-line coresident)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns × 3 tracepoints (queue + entry + exit) = ~150 ns
 * per csd round-trip.  Event rate: 100-100K csds/sec depending on
 * workload.  Per-sec overhead: 0.015-1.5%.
 *
 * Default-on safe at low end; opt-in via `CRUCIBLE_PERF_CSD=1`
 * for heavy multi-socket workloads.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - csd_queue_cpu fires for non-batched single-CPU dispatch; for
 *   `smp_call_function_many` the per-target queue events come via
 *   internal cpumask iteration (multiple csd_queue_cpu firings).
 * - Cross-socket csds are ~10× more expensive than intra-socket;
 *   userspace topology resolution applied at report time.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: ipi_events.bpf.c (raw IPI delivery — csd uses CALL_FUNCTION_SINGLE
 *   IPI, but csd tracepoints are the higher-level abstraction).
 * Sibling: irq_vectors.bpf.c (call_function_single_entry/exit are the
 *   x86-vector-level events; csd is the kernel-API level).
 * Aggregates with: SchedSwitch (a long csd handler can preempt our task).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
