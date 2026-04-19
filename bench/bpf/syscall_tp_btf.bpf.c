/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * syscall_tp_btf.bpf.c — Syscall latency via BTF-typed tracepoints.
 *
 * Functionally identical to syscall_latency.bpf.c but uses
 * SEC("tp_btf/sys_enter") and SEC("tp_btf/sys_exit") instead of
 * SEC("tracepoint/raw_syscalls/sys_enter") and sys_exit.
 *
 * Advantages over legacy raw_syscalls tracepoints:
 *   1. ~30% lower overhead (skips format string processing)
 *   2. CO-RE portable: args via BTF, survives kernel struct changes
 *   3. Direct typed access to syscall args
 *
 * BTF signatures (from /sys/kernel/btf/vmlinux):
 *   btf_trace_sys_enter(void *, struct pt_regs *regs, long syscall_id)
 *   btf_trace_sys_exit(void *, struct pt_regs *regs, long ret)
 *
 * Requires: CONFIG_DEBUG_INFO_BTF=y, /sys/kernel/btf/vmlinux present.
 * Minimum kernel: 5.5 (BTF raw tracepoints).
 */

#include "common.h"

/* ─── Maps (identical to syscall_latency.bpf.c) ─────────────────────── */

struct syscall_start_val {
    __u64 ts;
    __u32 nr;
    __u32 _pad;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
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

/* ─── BTF-typed tracepoint handlers ──────────────────────────────────── */

/*
 * tp_btf/sys_enter — BTF raw tracepoint for syscall entry.
 *
 * Args from BTF:
 *   ctx[0]: void *         (unused context pointer)
 *   ctx[1]: struct pt_regs * (register state at syscall entry)
 *   ctx[2]: long           syscall number (NR)
 */
SEC("tp_btf/sys_enter")
int handle_sys_enter_btf(u64 *ctx)
{
    if (!is_target())
        return 0;

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
 * tp_btf/sys_exit — BTF raw tracepoint for syscall exit.
 *
 * Args from BTF:
 *   ctx[0]: void *         (unused context pointer)
 *   ctx[1]: struct pt_regs * (register state)
 *   ctx[2]: long           return value (not used)
 */
SEC("tp_btf/sys_exit")
int handle_sys_exit_btf(u64 *ctx)
{
    if (!is_target())
        return 0;

    __u32 tid = get_tid();
    struct syscall_start_val *start = bpf_map_lookup_elem(&syscall_start, &tid);
    if (!start)
        return 0;

    __u64 delta = bpf_ktime_get_ns() - start->ts;
    __u32 nr = start->nr;
    bpf_map_delete_elem(&syscall_start, &tid);

    /* Update per-syscall stats */
    struct syscall_stats *stats = bpf_map_lookup_elem(&syscall_latency, &nr);
    if (stats) {
        __sync_fetch_and_add(&stats->count, 1);
        __sync_fetch_and_add(&stats->total_ns, delta);
        if (delta > stats->max_ns)
            stats->max_ns = delta;
        if (stats->min_ns == 0 || delta < stats->min_ns)
            stats->min_ns = delta;
    } else {
        struct syscall_stats new_stats = {
            .count = 1,
            .total_ns = delta,
            .max_ns = delta,
            .min_ns = delta,
        };
        bpf_map_update_elem(&syscall_latency, &nr, &new_stats, BPF_NOEXIST);
    }

    /* Emit to zero-copy timeline */
    __u32 tl_zero = 0;
    struct syscall_timeline *tl = bpf_map_lookup_elem(&syscall_timeline, &tl_zero);
    if (tl) {
        __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
        __u32 slot = (__u32)(idx & TIMELINE_MASK);
        if (slot < TIMELINE_CAPACITY) {
            tl->events[slot].duration_ns = delta;
            tl->events[slot].tid = tid;
            tl->events[slot].syscall_nr = nr;
            tl->events[slot].ts_ns = bpf_ktime_get_ns();
        }
    }

    /* Increment total counter */
    __u32 zero = 0;
    __u64 *cnt = bpf_map_lookup_elem(&total_syscalls, &zero);
    if (cnt)
        __sync_fetch_and_add(cnt, 1);

    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
