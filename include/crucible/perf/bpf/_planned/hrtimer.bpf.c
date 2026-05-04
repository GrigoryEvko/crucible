/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * hrtimer.bpf.c — Per-source hrtimer fire-frequency histogram.
 *
 * STATUS: doc-only stub.  Tier-B.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * A rogue high-frequency hrtimer (rogue profiler, noisy timer-leak
 * in another tenant, bad nanosleep loop in a sidecar) → wakeups that
 * look like preemption events in our SchedSwitch view.  hrtimer
 * tracepoints attribute them to the kernel-side function that armed
 * the timer.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/timer/hrtimer_setup       — timer initialised + armed
 *                                             (renamed from hrtimer_init in
 *                                              kernel 6.15; older kernels expose
 *                                              hrtimer_init under timer/ as well)
 *   - tracepoint/timer/hrtimer_start       — timer (re)started
 *   - tracepoint/timer/hrtimer_expire_entry — timer fired (begin)
 *   - tracepoint/timer/hrtimer_expire_exit  — timer handler returned
 *   - tracepoint/timer/hrtimer_cancel       — timer cancelled
 * Kernel min: 2.6.31 (with hrtimer_setup spelled hrtimer_init pre-6.15).
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - hrt_func_count: HASH[function_ptr → fire_count] — top-K hot
 *                   timer functions
 * - hrt_func_lat:   HASH[function_ptr → total_ns] — handler duration
 *                   per source (slack vs runtime)
 * - hrt_per_cpu_freq: PERCPU_ARRAY[1] — total hrtimer fires/sec
 *                     this CPU sees
 * - timeline:       ARRAY[1] + BPF_F_MMAPABLE — recent {func, cpu, ts}
 *                   for outlier diagnosis
 *
 * ─── WIRE CONTRACT ────────────────────────────────────────────────────
 * struct timeline_hrtimer_event {
 *     __u64 function;       // hrtimer.function (kallsyms)
 *     __u64 expires_ns;     // requested expiry time
 *     __u64 actual_ns;      // bpf_ktime_get_ns at expire_entry
 *                           // (slack = actual - expires)
 *     __u32 cpu;
 *     __u32 _pad;
 *     __u64 ts_ns;          // WRITTEN LAST
 * };  // 40 B
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns per tracepoint × 2-4 tracepoints per timer cycle.
 * Event rate: 100-10K timers/sec/CPU depending on workload.
 * Per-sec overhead: ~0.01-0.1%.  System-wide; no PID filter (timers
 * fire in IRQ context with current=swapper or arbitrary task).
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - hrtimer.function resolution: kallsyms lookup in userspace at
 *   read time.  ~10K function pointers in modern kernel.
 * - HRtimer slack tells you "kernel-side accuracy", but if YOU armed
 *   a timer, slack vs. expected delivery is your application bug to
 *   fix — out of scope for this facade.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: SchedSwitch (rogue hrtimer wake → spurious preempt).
 * Sibling: cpu_idle (hrtimer fires force CPU out of deep idle states).
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
