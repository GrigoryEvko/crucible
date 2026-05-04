/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * clocksource.bpf.c — clocksource_select / clocksource_watchdog observation.
 *
 * STATUS: doc-only stub.  DEPRIORITIZED — there is NO `clocksource/*`
 * tracepoint subsystem in the kernel (verified against kernel 6.17 +
 * mainline include/trace/events/).  The phenomenon is real but the
 * cited mechanism is hallucinated; replace with userspace polling of
 * /sys/devices/system/clocksource/clocksource0/current_clocksource
 * once per bench-iteration boundary (no BPF program required).
 * Single-event bench-reliability disaster early warning.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * If the kernel switches the system clocksource from TSC to HPET
 * mid-bench (post-CPU-migration jitter, AMD C-state wobbles,
 * hypervisor jitter, watchdog detection), then:
 *   - gettimeofday() / clock_gettime() goes from ~10 ns → ~1 µs
 *   - 100× cost increase on EVERY wall-clock measurement
 *   - bench latency numbers silently invalidated
 *   - the bench harness itself measures wrong by 100×
 *
 * Single rare event, catastrophic when it fires.  Currently invisible.
 * We'd see "everything got 100× slower mid-bench" with no idea why.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: kprobe (NO `clocksource/*` tracepoint subsystem
 * exists in mainline as of 6.17).
 * Attachment points (kprobe on internal clocksource functions in
 * kernel/time/clocksource.c):
 *   - kprobe/clocksource_select                       — clocksource selection (static)
 *   - kprobe/clocksource_watchdog_work                — instability detected
 *   - kprobe/__clocksource_change_rating              — rating change → potential switch
 *   - tracepoint/timer/tick_stop                      — tick stopped (idle)  [REAL]
 *
 * Practical alternative (preferred): userspace polls
 *   /sys/devices/system/clocksource/clocksource0/current_clocksource
 * once per bench-iteration boundary; ~10 µs sysfs read every ~ms is
 * cheaper than maintaining BTF-pinned kprobes on internal functions
 * whose signatures change without ABI guarantees.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - cs_change_count: ARRAY[1] — total clocksource switches
 * - cs_unstable_count: ARRAY[1] — instability detections
 * - last_cs_name: ARRAY[1] — last selected clocksource name (16 B)
 * - cs_timeline: ARRAY[1] + BPF_F_MMAPABLE — every change event
 *                with ts, from-name, to-name, reason
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: typically 0/sec (clocksource is
 * stable in normal operation).  Effectively free always-on.
 *
 * Bench harness emits a banner on detection:
 *   *** CLOCKSOURCE CHANGED tsc → hpet AT 14.2s — bench results
 *       AFTER THIS POINT MEASURED WITH 100× MORE EXPENSIVE TIMER ***
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - clocksource_unstable fires from the watchdog timer (typically
 *   every ~500 ms); detection has up to 500 ms latency.
 * - VM environments: hypervisor's KVM-clock or paravirtual clock
 *   wraps similar concerns; tracepoint covers all variants.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Bench-reliability pair with: rdtscp/tsc-stability checking in
 *   bench harness's Timer subsystem.  This is the kernel's view;
 *   the harness's `tsc_freq_hz()` calibration check is the
 *   userspace view.  Both should agree.
 *
 * Out-of-band: `/sys/devices/system/clocksource/.../available_clocksource`
 *   tells you what alternatives exist (don't need BPF for that).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
