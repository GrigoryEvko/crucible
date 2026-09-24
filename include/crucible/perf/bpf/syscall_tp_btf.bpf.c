/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

struct syscall_start_val {
    __u64 ts;
    __u32 nr;
    __u32 _pad;
};

/* An enter whose exit never arrives leaves an orphan here. A thread killed
 * mid-call, an exec that replaces the thread, or a target filter that accepts
 * the enter and rejects the exit all produce one. LRU_HASH evicts them under
 * capacity pressure. A plain HASH would fill to the entry limit and then
 * reject every new insert. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, struct syscall_start_val);
} syscall_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, struct syscall_stats);
} syscall_latency SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} total_syscalls SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct syscall_timeline);
} syscall_timeline SEC(".maps");

/*
 * The raw tracepoint hands over its arguments as an untyped array. Slot 0 is
 * an unused context pointer, slot 1 the register state at entry and slot 2 the
 * syscall number.
 *
 * A BTF tracepoint needs a kernel built with CONFIG_DEBUG_INFO_BTF, which is
 * what puts /sys/kernel/btf/vmlinux in place, and kernel 5.5 or newer.
 */
SEC("tp_btf/sys_enter")
int handle_sys_enter_btf(u64* ctx) {
    if (!is_target()) return 0;

    __u32 tid = get_tid();
    long syscall_id = (long)ctx[2];

    struct syscall_start_val val = {
        .ts = bpf_ktime_get_ns(),
        .nr = (__u32)syscall_id,
    };
    bpf_map_update_elem(&syscall_start, &tid, &val, BPF_ANY);

    return 0;
}

/*
 * The argument array here holds an unused context pointer, the register state
 * and the syscall return value.
 */
SEC("tp_btf/sys_exit")
int handle_sys_exit_btf(u64* ctx) {
    if (!is_target()) return 0;

    __u64 now = bpf_ktime_get_ns();

    __u32 tid = get_tid();
    struct syscall_start_val* start = bpf_map_lookup_elem(&syscall_start, &tid);
    if (!start) return 0;

    __u64 delta = now - start->ts;
    __u32 nr = start->nr;
    bpf_map_delete_elem(&syscall_start, &tid);

    struct syscall_stats* stats = bpf_map_lookup_elem(&syscall_latency, &nr);
    if (stats) {
        __sync_fetch_and_add(&stats->count, 1);
        __sync_fetch_and_add(&stats->total_ns, delta);
        if (delta > stats->max_ns) stats->max_ns = delta;
        if (stats->min_ns == 0 || delta < stats->min_ns) stats->min_ns = delta;
    } else {
        struct syscall_stats new_stats = {
            .count = 1,
            .total_ns = delta,
            .max_ns = delta,
            .min_ns = delta,
        };
        bpf_map_update_elem(&syscall_latency, &nr, &new_stats, BPF_NOEXIST);
    }

    __u32 tl_zero = 0;
    struct syscall_timeline* tl = bpf_map_lookup_elem(&syscall_timeline, &tl_zero);
    if (tl) {
        __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
        __u32 slot = (__u32)(idx & TIMELINE_MASK);
        if (slot < TIMELINE_CAPACITY) {
            tl->events[slot].duration_ns = delta;
            tl->events[slot].tid = tid;
            tl->events[slot].syscall_nr = nr;
            /* The memory clobber stops the compiler from sinking the three
             * stores above past the ts_ns store. It pairs with the acquire
             * load of ts_ns in the reader. */
            __asm__ __volatile__("" ::: "memory");
            tl->events[slot].ts_ns = now; /* completion marker */
        }
    }

    __u32 zero = 0;
    __u64* cnt = bpf_map_lookup_elem(&total_syscalls, &zero);
    if (cnt) __sync_fetch_and_add(cnt, 1);

    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
