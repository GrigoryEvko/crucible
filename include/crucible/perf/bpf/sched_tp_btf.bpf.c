/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sched_tp_btf.bpf.c — Off-CPU analysis via BTF-typed sched_switch tracepoint.
 *
 * Functionally identical to sched_switch.bpf.c but uses SEC("tp_btf/sched_switch")
 * instead of SEC("tracepoint/sched/sched_switch").
 *
 * Advantages over the legacy tracepoint path:
 *   1. ~30% lower overhead (skips tracepoint format string processing)
 *   2. CO-RE portable: args accessed via BTF, survives kernel struct changes
 *   3. Direct struct task_struct access (prev->pid instead of ctx->prev_pid)
 *   4. Fires in prev's context — bpf_get_stackid captures correct stack
 *
 * BTF signature (from /sys/kernel/btf/vmlinux):
 *   btf_trace_sched_switch(void *, bool preempt, struct task_struct *prev,
 *                          struct task_struct *next, unsigned int prev_state)
 *
 * Requires: CONFIG_DEBUG_INFO_BTF=y, /sys/kernel/btf/vmlinux present.
 * Minimum kernel: 5.5 (BTF raw tracepoints).
 */

#include "common.h"

/* ─── Maps (identical to sched_switch.bpf.c) ─────────────────────────── */

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __u64);
} switch_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __s32);
} switch_stack SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, struct offcpu_key);
    __type(value, struct offcpu_val);
} offcpu SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_STACK_TRACE);
    __uint(max_entries, MAX_STACKS);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, MAX_STACK_DEPTH * sizeof(__u64));
} stacks SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __u8);
} our_tids SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} cs_count SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct sched_timeline);
} sched_timeline SEC(".maps");

/* ─── BTF-typed tracepoint handler ────────────────────────────────────── */

/*
 * tp_btf/sched_switch — BTF raw tracepoint variant.
 *
 * Args from BTF (typed, CO-RE relocated):
 *   ctx[0]: void *         (unused context pointer)
 *   ctx[1]: bool           preempt flag
 *   ctx[2]: task_struct *  prev task (being switched OUT)
 *   ctx[3]: task_struct *  next task (being switched IN)
 *   ctx[4]: unsigned int   prev_state
 *
 * Fires in PREV task's context — bpf_get_stackid() captures prev's stack.
 */
SEC("tp_btf/sched_switch")
int handle_sched_switch_btf(u64 *ctx)
{
    /* Extract typed arguments from BTF context */
    struct task_struct *prev = (struct task_struct *)ctx[2];
    struct task_struct *next = (struct task_struct *)ctx[3];

    __u64 ts = bpf_ktime_get_ns();
    __u32 prev_pid = BPF_CORE_READ(prev, pid);
    __u32 prev_tgid = BPF_CORE_READ(prev, tgid);
    __u32 next_pid = BPF_CORE_READ(next, pid);

    /* ── Switch OUT: prev belongs to our process ─────────────────── */
    if (prev_tgid == target_tgid) {
        bpf_map_update_elem(&switch_start, &prev_pid, &ts, BPF_ANY);

        /* Capture userspace stack — correct because we're in prev's context */
        __s32 sid = bpf_get_stackid(ctx, &stacks,
                                     BPF_F_USER_STACK | BPF_F_FAST_STACK_CMP);
        if (sid >= 0) {
            bpf_map_update_elem(&switch_stack, &prev_pid, &sid, BPF_ANY);
        }

        /* Increment context switch counter */
        __u32 zero = 0;
        __u64 *cnt = bpf_map_lookup_elem(&cs_count, &zero);
        if (cnt)
            __sync_fetch_and_add(cnt, 1);
    }

    /* ── Switch IN: next belongs to our process ──────────────────── */
    __u8 *is_ours = bpf_map_lookup_elem(&our_tids, &next_pid);
    if (is_ours) {
        __u64 *start_ts = bpf_map_lookup_elem(&switch_start, &next_pid);
        if (start_ts && *start_ts > 0) {
            __u64 delta = ts - *start_ts;

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

            /* Emit to zero-copy timeline */
            __u32 tl_zero = 0;
            struct sched_timeline *tl = bpf_map_lookup_elem(&sched_timeline, &tl_zero);
            if (tl) {
                __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
                __u32 slot = (__u32)(idx & TIMELINE_MASK);
                if (slot < TIMELINE_CAPACITY) {
                    tl->events[slot].off_cpu_ns = delta;
                    tl->events[slot].tid = next_pid;
                    tl->events[slot].on_cpu = bpf_get_smp_processor_id();
                    tl->events[slot].ts_ns = ts;
                }
            }

            bpf_map_delete_elem(&switch_start, &next_pid);
        }
    }

    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
