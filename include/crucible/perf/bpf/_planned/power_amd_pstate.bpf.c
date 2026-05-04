/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * power_amd_pstate.bpf.c — AMD P-state EPP / performance changes.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  AMD-only
 * (verified against /sys/kernel/tracing/events/amd_cpu/ on 6.17 +
 * verified that this dev host exposes amd_df + amd_l3 PMUs i.e. AMD
 * platform).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * AMD's P-state driver (`amd-pstate`) drives CPU performance state
 * transitions via CPPC (Collaborative Processor Performance Control).
 * Two flavors:
 *
 *   - amd_pstate_perf       — performance request changed
 *   - amd_pstate_epp_perf   — Energy-Performance Preference + perf changed
 *                             (active mode only)
 *
 * Bench-relevance: at the start of a bench window the P-state may
 * still be ramping (EPP=performance asks for max, but firmware needs
 * ~500 µs to actually deliver max frequency).  Mid-bench EPP changes
 * (kernel adapts based on load history) are silent today; result:
 * "p99 latency randomly 2× slower in iteration 7" with no signal as
 * to why (the P-state dropped from boost to base under low utilization).
 *
 * Crucible bench harness already pins CPU and disables governor for
 * variance discipline (CLAUDE.md §VIII), but production / Augur
 * deployments don't have that luxury — knowing WHEN P-state shifted
 * is necessary to interpret latency variance.
 *
 * Intel equivalent (intel_pstate / cpufreq tracepoints) is NOT
 * planned here; intel_pstate exposes via the existing
 * `power/cpu_frequency` tracepoint which `vmscan_ext.bpf.c` plan
 * touches.  Add Intel-side stub if/when an Intel facade matters.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17):
 *   - tracepoint/amd_cpu/amd_pstate_perf       — perf request change
 *   - tracepoint/amd_cpu/amd_pstate_epp_perf   — EPP + perf change
 *
 * Per-event payload includes:
 *   - cpu_id (which CPU)
 *   - min_perf, target_perf, capacity (CPPC values)
 *   - epp (only for *_epp_perf variant)
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_cpu_state: PERCPU_ARRAY[1] — current {target_perf, epp,
 *                                              last_change_ts}
 * - change_count: PERCPU_ARRAY[1] — total transitions per CPU this run
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE — every transition with cpu,
 *             from→to perf, ts
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_amd_pstate_event {
 *     uint64_t ts_ns;                // WRITTEN LAST
 *     uint16_t cpu;
 *     uint8_t  from_perf;
 *     uint8_t  to_perf;
 *     uint8_t  from_epp;             // 0 if non-active mode
 *     uint8_t  to_epp;
 *     uint8_t  _pad[2];
 * };  // 16 B
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: 1-100/sec on a busy host with EPP
 * active mode (kernel adapts per-load).  Effectively free always-on.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - AMD-only (Zen2 EPYC+, Zen3+ Ryzen with amd-pstate driver enabled).
 *   Detect via /sys/devices/system/cpu/amd_pstate/status presence at
 *   facade load; bail with diagnostic if not present.
 * - Per-policy events fire from the requesting kernel context, not
 *   firmware delivery time — actual frequency ramp lag (~10-100 µs)
 *   between trace event and observable rdpmc cycle rate.
 * - Cross-correlate with PmuMsrAperfMperfReader (planned Tier-3) for
 *   actual delivered frequency ground truth.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: `power/cpu_frequency` tracepoint (cross-vendor; coarser).
 * Sibling: `power/cpu_idle` (existing `cpu_idle.bpf.c`) — pairs with
 *   this for "CPU was here" full attribution.
 * Bench-reliability complement to: PmuMsrAperfMperfReader (planned)
 *   for "did the requested perf actually deliver?".
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
