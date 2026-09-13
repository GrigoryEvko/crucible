/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

#define FUTEX_WAIT 0
#define FUTEX_WAIT_BITSET 9
#define FUTEX_LOCK_PI 6

/* An enter whose exit never arrives leaves an orphan here. A thread killed
 * mid-wait, a syscall that returns through a signal, or a target filter that
 * accepts the enter and rejects the exit all produce one. LRU_HASH evicts them
 * under capacity pressure. A plain HASH would fill to the entry limit and then
 * reject every new insert. */
struct wait_info {
    __u64 addr;
    __u64 ts;
    __s32 stack_id;
    __u32 _pad;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, struct wait_info);
} wait_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, struct lock_key);
    __type(value, struct lock_val);
} contention SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_STACK_TRACE);
    __uint(max_entries, MAX_STACKS);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, MAX_STACK_DEPTH * sizeof(__u64));
} lock_stacks SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} lock_wait_count SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct lock_timeline);
} lock_timeline SEC(".maps");

/*
 * The syscall arguments arrive as a raw array. Slot 0 holds the futex user
 * address and slot 1 holds the operation, then val, timespec, uaddr2 and val3.
 */
SEC("tracepoint/syscalls/sys_enter_futex")
int handle_futex_enter(struct trace_event_raw_sys_enter* ctx) {
    if (!is_target()) return 0;

    /* The high bits carry FUTEX_PRIVATE_FLAG and FUTEX_CLOCK_REALTIME, which
     * do not change which operation this is. */
    int op = (int)ctx->args[1] & 0x7F;

    if (op != FUTEX_WAIT && op != FUTEX_WAIT_BITSET && op != FUTEX_LOCK_PI) return 0;

    __u32 tid = get_tid();
    __s32 sid = bpf_get_stackid(ctx, &lock_stacks, BPF_F_USER_STACK | BPF_F_FAST_STACK_CMP);

    struct wait_info info = {
        .addr = ctx->args[0],
        .ts = bpf_ktime_get_ns(),
        .stack_id = sid >= 0 ? sid : -1,
    };
    bpf_map_update_elem(&wait_start, &tid, &info, BPF_ANY);

    return 0;
}

SEC("tracepoint/syscalls/sys_exit_futex")
int handle_futex_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;

    __u64 now = bpf_ktime_get_ns();

    __u32 tid = get_tid();
    struct wait_info* info = bpf_map_lookup_elem(&wait_start, &tid);
    if (!info) return 0;

    __u64 delta = now - info->ts;
    struct lock_key key = {
        .addr = info->addr,
        .stack_id = info->stack_id,
    };

    bpf_map_delete_elem(&wait_start, &tid);

    __u32 tl_zero = 0;
    struct lock_timeline* tl = bpf_map_lookup_elem(&lock_timeline, &tl_zero);
    if (tl) {
        __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
        __u32 slot = (__u32)(idx & TIMELINE_MASK);
        if (slot < TIMELINE_CAPACITY) {
            tl->events[slot].futex_addr = info->addr;
            tl->events[slot].wait_ns = delta;
            tl->events[slot].tid = tid;
            /* The memory clobber stops the compiler from sinking the three
             * stores above past the ts_ns store. It pairs with the acquire
             * load of ts_ns in the reader. */
            __asm__ __volatile__("" ::: "memory");
            tl->events[slot].ts_ns = now; /* completion marker */
        }
    }

    struct lock_val* val = bpf_map_lookup_elem(&contention, &key);
    if (val) {
        __sync_fetch_and_add(&val->total_wait_ns, delta);
        __sync_fetch_and_add(&val->count, 1);
        if (delta > val->max_wait_ns) val->max_wait_ns = delta;
    } else {
        struct lock_val new_val = {
            .total_wait_ns = delta,
            .count = 1,
            .max_wait_ns = delta,
        };
        bpf_map_update_elem(&contention, &key, &new_val, BPF_NOEXIST);
    }

    __u32 zero = 0;
    __u64* cnt = bpf_map_lookup_elem(&lock_wait_count, &zero);
    if (cnt) __sync_fetch_and_add(cnt, 1);

    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
