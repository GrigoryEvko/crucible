/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

/* A thread switched out and never seen switching back in leaves an orphan
 * here. LRU_HASH evicts those under insert pressure. A plain HASH would fill
 * to the entry limit and then reject every new insert. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __u64);
} switch_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
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

/* Userspace fills this map with the tids of the target process. Nothing in
 * this program writes to it. It is what identifies the incoming task, because
 * the tracepoint runs in the outgoing task's context, so
 * bpf_get_current_pid_tgid() says nothing about the task switching in. */
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

/*
 * The tracepoint fires in the context of the outgoing task, so
 * bpf_get_current_pid_tgid() returns that task's identity and is_target()
 * therefore tests the outgoing side.
 */
SEC("tracepoint/sched/sched_switch")
int handle_sched_switch(struct trace_event_raw_sched_switch* ctx) {
    __u64 ts = bpf_ktime_get_ns();
    __u32 prev_pid = ctx->prev_pid;
    __u32 next_pid = ctx->next_pid;

    if (is_target()) {
        /* prev_state is deliberately not consulted. A non-zero value means the
         * task sleeps voluntarily and a zero value means it is runnable and
         * preempted, and both count as time off the CPU. */
        bpf_map_update_elem(&switch_start, &prev_pid, &ts, BPF_ANY);

        __s32 sid = bpf_get_stackid(ctx, &stacks, BPF_F_USER_STACK | BPF_F_FAST_STACK_CMP);
        if (sid >= 0) {
            bpf_map_update_elem(&switch_stack, &prev_pid, &sid, BPF_ANY);
        }

        __u32 zero = 0;
        __u64* cnt = bpf_map_lookup_elem(&cs_count, &zero);
        if (cnt) __sync_fetch_and_add(cnt, 1);
    }

    __u8* is_ours = bpf_map_lookup_elem(&our_tids, &next_pid);
    if (is_ours) {
        __u64* start_ts = bpf_map_lookup_elem(&switch_start, &next_pid);
        if (start_ts && *start_ts > 0) {
            __u64 delta = ts - *start_ts;

            __s32* sid = bpf_map_lookup_elem(&switch_stack, &next_pid);
            __s32 stack_id = sid ? *sid : -1;

            struct offcpu_key key = {
                .stack_id = stack_id,
                .tid = next_pid,
            };

            struct offcpu_val* val = bpf_map_lookup_elem(&offcpu, &key);
            if (val) {
                __sync_fetch_and_add(&val->total_ns, delta);
                __sync_fetch_and_add(&val->count, 1);
                if (delta > val->max_ns) val->max_ns = delta;
            } else {
                struct offcpu_val new_val = {
                    .total_ns = delta,
                    .count = 1,
                    .max_ns = delta,
                };
                bpf_map_update_elem(&offcpu, &key, &new_val, BPF_NOEXIST);
            }

            __u32 tl_zero = 0;
            struct sched_timeline* tl = bpf_map_lookup_elem(&sched_timeline, &tl_zero);
            if (tl) {
                __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
                __u32 slot = (__u32)(idx & TIMELINE_MASK);
                if (slot < TIMELINE_CAPACITY) {
                    tl->events[slot].off_cpu_ns = delta;
                    tl->events[slot].tid = next_pid;
                    tl->events[slot].on_cpu = bpf_get_smp_processor_id();
                    /* The memory clobber stops the compiler from sinking
                     * the three stores above past the ts_ns store. It pairs
                     * with the acquire load of ts_ns in the reader. */
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
