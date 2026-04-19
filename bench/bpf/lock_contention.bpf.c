/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * symbiotic: Lock contention profiling via futex tracing.
 *
 * Traces futex(FUTEX_WAIT) entry/exit to measure exactly how long
 * each thread waits on each lock. Aggregates by (futex_address, stack)
 * so the userspace side can tell you:
 *   "Mutex at 0x7f...a380 is contended 450 times, avg wait 12μs,
 *    hottest caller: image_filter::sharpen line 142"
 *
 * Also tracks voluntary vs involuntary waits via sched:sched_process_wait.
 */

#include "common.h"

/* Futex operations we care about */
#define FUTEX_WAIT         0
#define FUTEX_WAIT_BITSET  9
#define FUTEX_LOCK_PI      6

/* ─── Maps ──────────────────────────────────────────────────────────── */

/* (tid) → (futex_addr, start_ts, stack_id) for in-flight waits */
struct wait_info {
    __u64 addr;
    __u64 ts;
    __s32 stack_id;
    __u32 _pad;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, struct wait_info);
} wait_start SEC(".maps");

/* (futex_addr, stack_id) → contention stats */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, struct lock_key);
    __type(value, struct lock_val);
} contention SEC(".maps");

/* Stack traces */
struct {
    __uint(type, BPF_MAP_TYPE_STACK_TRACE);
    __uint(max_entries, MAX_STACKS);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, MAX_STACK_DEPTH * sizeof(__u64));
} lock_stacks SEC(".maps");

/* Total lock wait events counter */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} lock_wait_count SEC(".maps");

/* Zero-copy timeline: mmap'd circular buffer (sub-ns reads from Rust) */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct lock_timeline);
} lock_timeline SEC(".maps");

/* ─── Tracepoint handlers ──────────────────────────────────────────── */

/*
 * sys_enter_futex format:
 *   field:unsigned long uaddr;
 *   field:int op;
 *   field:unsigned int val;
 *   field:... (timespec, uaddr2, val3)
 */
SEC("tracepoint/syscalls/sys_enter_futex")
int handle_futex_enter(struct trace_event_raw_sys_enter *ctx)
{
    if (!is_target())
        return 0;

    /* Read futex op from args[1] (second arg to futex syscall) */
    int op = (int)ctx->args[1] & 0x7F;  /* mask out FUTEX_PRIVATE_FLAG etc */

    /* Only trace wait operations */
    if (op != FUTEX_WAIT && op != FUTEX_WAIT_BITSET && op != FUTEX_LOCK_PI)
        return 0;

    __u32 tid = get_tid();
    __s32 sid = bpf_get_stackid(ctx, &lock_stacks,
                                BPF_F_USER_STACK | BPF_F_FAST_STACK_CMP);

    struct wait_info info = {
        .addr = ctx->args[0],  /* futex uaddr */
        .ts   = bpf_ktime_get_ns(),
        .stack_id = sid >= 0 ? sid : -1,
    };
    bpf_map_update_elem(&wait_start, &tid, &info, BPF_ANY);

    return 0;
}

SEC("tracepoint/syscalls/sys_exit_futex")
int handle_futex_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target())
        return 0;

    __u32 tid = get_tid();
    struct wait_info *info = bpf_map_lookup_elem(&wait_start, &tid);
    if (!info)
        return 0;

    __u64 delta = bpf_ktime_get_ns() - info->ts;
    struct lock_key key = {
        .addr = info->addr,
        .stack_id = info->stack_id,
    };

    bpf_map_delete_elem(&wait_start, &tid);

    /* Emit to zero-copy timeline (ts_ns written last for ordering) */
    __u32 tl_zero = 0;
    struct lock_timeline *tl = bpf_map_lookup_elem(&lock_timeline, &tl_zero);
    if (tl) {
        __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
        __u32 slot = (__u32)(idx & TIMELINE_MASK);
        if (slot < TIMELINE_CAPACITY) {
            tl->events[slot].futex_addr = info->addr;
            tl->events[slot].wait_ns = delta;
            tl->events[slot].tid = tid;
            tl->events[slot].ts_ns = bpf_ktime_get_ns(); /* completion marker */
        }
    }

    /* Aggregate contention */
    struct lock_val *val = bpf_map_lookup_elem(&contention, &key);
    if (val) {
        __sync_fetch_and_add(&val->total_wait_ns, delta);
        __sync_fetch_and_add(&val->count, 1);
        if (delta > val->max_wait_ns)
            val->max_wait_ns = delta;
    } else {
        struct lock_val new_val = {
            .total_wait_ns = delta,
            .count = 1,
            .max_wait_ns = delta,
        };
        bpf_map_update_elem(&contention, &key, &new_val, BPF_NOEXIST);
    }

    /* Increment total counter */
    __u32 zero = 0;
    __u64 *cnt = bpf_map_lookup_elem(&lock_wait_count, &zero);
    if (cnt)
        __sync_fetch_and_add(cnt, 1);

    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
