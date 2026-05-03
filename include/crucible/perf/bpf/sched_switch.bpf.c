/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * symbiotic: Off-CPU analysis via sched_switch tracepoint.
 *
 * When a thread in our process gets switched OUT (sleeping, blocked,
 * preempted), we record the timestamp and userspace stack trace.
 * When it gets switched back IN, we compute the off-CPU duration and
 * aggregate it by stack trace in a BPF hash map.
 *
 * The userspace side (aya) reads the maps periodically to produce
 * off-CPU flame graphs and scheduling statistics.
 */

#include "common.h"

/* ─── Maps ──────────────────────────────────────────────────────────── */

/* tid → switch-out timestamp.
 *
 * GAPS-004b-AUDIT (2026-05-04): LRU_HASH (not plain HASH) — orphaned
 * entries (thread switched OUT but never observed switching IN, e.g.
 * non-our_tids threads or threads that exited) are auto-evicted on
 * insert pressure rather than accumulating to MAX_ENTRIES and silently
 * blocking new inserts.  Zero per-event cost vs HASH; the kernel's LRU
 * list update piggybacks on the existing bucket walk. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __u64);
} switch_start SEC(".maps");

/* tid → stack_id captured at switch-out.  LRU_HASH for the same
 * reason as switch_start — see comment above. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __s32);
} switch_stack SEC(".maps");

/* (stack_id, tid) → aggregated off-CPU time */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, struct offcpu_key);
    __type(value, struct offcpu_val);
} offcpu SEC(".maps");

/* Stack traces (kernel-managed, indexed by stack_id) */
struct {
    __uint(type, BPF_MAP_TYPE_STACK_TRACE);
    __uint(max_entries, MAX_STACKS);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, MAX_STACK_DEPTH * sizeof(__u64));
} stacks SEC(".maps");

/* Set of TIDs belonging to our process (populated from userspace).
 * Needed because on switch-in we're in next_task context but
 * the tracepoint fires in prev_task context — we can't call
 * bpf_get_current_pid_tgid() for the next task. */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __u8);
} our_tids SEC(".maps");

/* Total context switch counter for our process */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} cs_count SEC(".maps");

/* Zero-copy timeline: mmap'd circular buffer (sub-ns reads from Rust) */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct sched_timeline);
} sched_timeline SEC(".maps");

/* ─── Tracepoint handler ───────────────────────────────────────────── */

/*
 * Tracepoint args from /sys/kernel/debug/tracing/events/sched/sched_switch/format:
 *
 *  prev_comm[16], prev_pid, prev_prio, prev_state
 *  next_comm[16], next_pid, next_prio
 *
 * The tracepoint fires in the context of the PREV task (the one being
 * switched out), so bpf_get_current_pid_tgid() returns prev's pid/tgid.
 */
SEC("tracepoint/sched/sched_switch")
int handle_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    __u64 ts = bpf_ktime_get_ns();
    __u32 prev_pid = ctx->prev_pid;
    __u32 next_pid = ctx->next_pid;

    /* ── Switch OUT: prev_pid belongs to our process ─────────────── */
    if (is_target()) {
        /*
         * prev_state != 0 → task is voluntarily sleeping (S/D/etc).
         * prev_state == 0 → task is runnable but preempted (R+).
         * We track both — preemption is also off-CPU time.
         */
        bpf_map_update_elem(&switch_start, &prev_pid, &ts, BPF_ANY);

        /* Capture userspace stack at the point of switch-out */
        __s32 sid = bpf_get_stackid(ctx, &stacks, BPF_F_USER_STACK | BPF_F_FAST_STACK_CMP);
        if (sid >= 0) {
            bpf_map_update_elem(&switch_stack, &prev_pid, &sid, BPF_ANY);
        }

        /* Increment context switch counter */
        __u32 zero = 0;
        __u64 *cnt = bpf_map_lookup_elem(&cs_count, &zero);
        if (cnt)
            __sync_fetch_and_add(cnt, 1);
    }

    /* ── Switch IN: next_pid belongs to our process ──────────────── */
    __u8 *is_ours = bpf_map_lookup_elem(&our_tids, &next_pid);
    if (is_ours) {
        __u64 *start_ts = bpf_map_lookup_elem(&switch_start, &next_pid);
        if (start_ts && *start_ts > 0) {
            __u64 delta = ts - *start_ts;

            /* Look up the stack captured at switch-out */
            __s32 *sid = bpf_map_lookup_elem(&switch_stack, &next_pid);
            __s32 stack_id = sid ? *sid : -1;

            struct offcpu_key key = {
                .stack_id = stack_id,
                .tid = next_pid,
            };

            struct offcpu_val *val = bpf_map_lookup_elem(&offcpu, &key);
            if (val) {
                __sync_fetch_and_add(&val->total_ns, delta);
                __sync_fetch_and_add(&val->count, 1);
                if (delta > val->max_ns)
                    val->max_ns = delta;
            } else {
                struct offcpu_val new_val = {
                    .total_ns = delta,
                    .count = 1,
                    .max_ns = delta,
                };
                bpf_map_update_elem(&offcpu, &key, &new_val, BPF_NOEXIST);
            }

            /* Emit to zero-copy timeline (ts_ns written last for ordering) */
            __u32 tl_zero = 0;
            struct sched_timeline *tl = bpf_map_lookup_elem(&sched_timeline, &tl_zero);
            if (tl) {
                __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
                __u32 slot = (__u32)(idx & TIMELINE_MASK);
                if (slot < TIMELINE_CAPACITY) {
                    tl->events[slot].off_cpu_ns = delta;
                    tl->events[slot].tid = next_pid;
                    tl->events[slot].on_cpu = bpf_get_smp_processor_id();
                    /* Compiler barrier — GAPS-004b-AUDIT (2026-05-04).
                     * Forces clang to emit the prior 3 stores BEFORE
                     * the ts_ns store; without this, -O2 may reorder
                     * and break the "ts_ns LAST as completion marker"
                     * contract.  Zero machine cost (asm volatile with
                     * empty body emits no instruction; the "memory"
                     * clobber tells the compiler not to reorder
                     * across this point).  Pairs with the userspace
                     * reader's __atomic_load_n(&ts_ns, ACQUIRE). */
                    __asm__ __volatile__("" ::: "memory");
                    tl->events[slot].ts_ns = ts; /* completion marker */
                }
            }

            bpf_map_delete_elem(&switch_start, &next_pid);
        }
    }

    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
