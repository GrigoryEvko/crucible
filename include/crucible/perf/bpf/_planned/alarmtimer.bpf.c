/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * alarmtimer.bpf.c — POSIX alarm sleep/wake observation.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-3 addition.  Verified
 * against /sys/kernel/tracing/events/alarmtimer/ on 6.17 (4 events).
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * `alarmtimer` is the kernel's wakeup-from-suspend timer subsystem,
 * exposed to userspace via `timer_create(CLOCK_REALTIME_ALARM)` /
 * `CLOCK_BOOTTIME_ALARM` (POSIX timer flavors).  Used by:
 *   - systemd-run --on-active=...  (timed unit triggers)
 *   - Wake-from-S3 alarm scheduling
 *   - Background sync daemons (rsync, snapper, etc.) with periodic schedules
 *
 * Bench-relevance: alarmtimer wakes can fire during bench windows
 * even on idle hosts — a third party's `at` job, a backup script, an
 * administrative cron.  Fires preempt our task.  Currently invisible
 * to bench attribution.
 *
 * Production-relevance: scheduler determinism for time-sensitive
 * dispatch (when did the next periodic event fire vs when we expected)
 * — bounded by alarmtimer wakeup precision.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points (verified on 6.17 — 4 events):
 *   - tracepoint/alarmtimer/alarmtimer_start    — alarm armed
 *   - tracepoint/alarmtimer/alarmtimer_fired    — alarm wakeup fired
 *   - tracepoint/alarmtimer/alarmtimer_cancel   — alarm cancelled
 *   - tracepoint/alarmtimer/alarmtimer_suspend  — alarm into suspend
 *
 * Per-event payload includes alarm address, alarm type (CLOCK_REALTIME_ALARM
 * vs CLOCK_BOOTTIME_ALARM), expires timestamp.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - active_alarms: HASH[alarm_addr → start_ts]  LRU max 256
 * - per_pid_summary: HASH[tid → {fire_count, total_drift_ns}]
 *                    drift = actual_fire_ts - expected_fire_ts
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_alarmtimer_event {
 *     uint64_t alarm_addr;
 *     int64_t  drift_ns;          // fire_ts - expected_ts
 *     uint32_t tid;
 *     uint8_t  event_kind;        // start/fired/cancel/suspend
 *     uint8_t  alarm_type;        // CLOCK_REALTIME_ALARM / CLOCK_BOOTTIME_ALARM
 *     uint8_t  _pad[2];
 *     uint64_t ts_ns;             // WRITTEN LAST
 * };  // 32 B (cache-line coresident)
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: typically very low (1-10/sec on
 * lightly-used host; bursts when systemd timers fire).  Effectively
 * free always-on.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - alarmtimer is a SUPERSET of regular hrtimer for the wakeup-capable
 *   variants; doesn't capture all hrtimer activity (use hrtimer.bpf.c
 *   for that broader set).
 * - Drift measurement requires kernel ≥ 5.x where alarmtimer_fired
 *   payload includes the originally-requested expires timestamp.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: hrtimer.bpf.c (planned) — superset of timer-fire activity.
 *   alarmtimer is a specific subset (wakeup-capable timers only).
 * Sibling: cpu_idle.bpf.c (planned) — alarmtimer_suspend correlates
 *   with system entering deep idle.
 * Aggregates with: SchedSwitch — alarmtimer_fired triggers a
 *   sched_switch on the target task's CPU.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
