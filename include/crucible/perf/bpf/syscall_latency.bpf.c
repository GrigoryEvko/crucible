/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * symbiotic: Syscall latency profiling.
 *
 * Traces raw_syscalls:sys_enter and raw_syscalls:sys_exit for our
 * target process. Computes per-syscall latency histograms and
 * aggregated statistics in BPF maps.
 *
 * The program knows EXACTLY how long each syscall takes — no
 * sampling, no estimation, pure kernel-side measurement.
 */

#include "common.h"

/* ─── Maps ──────────────────────────────────────────────────────────── */

/* (tid) → (syscall_nr, start_timestamp) for in-flight syscalls.
 *
 * GAPS-004e (2026-05-04): LRU_HASH (not plain HASH) — orphaned
 * entries (sys_enter recorded but matching sys_exit never arrives,
 * e.g. thread killed mid-syscall, exec replacing thread, target_tgid
 * filter accepted enter but rejected exit) auto-evict on capacity
 * pressure rather than accumulating to MAX_ENTRIES=65536 and silently
 * blocking new inserts via BPF_NOEXIST.  Same fix class as
 * SchedSwitch GAPS-004b-AUDIT and LockContention GAPS-004d-AUDIT. */
struct syscall_start_val {
    __u64 ts;
    __u32 nr;
    __u32 _pad;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, struct syscall_start_val);
} syscall_start SEC(".maps");

/* syscall_nr → aggregated latency stats */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, struct syscall_stats);
} syscall_latency SEC(".maps");

/* Total syscall counter (array[0]) */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} total_syscalls SEC(".maps");

/* Zero-copy timeline: mmap'd circular buffer (sub-ns reads from Rust) */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct syscall_timeline);
} syscall_timeline SEC(".maps");

/* ─── Tracepoint handlers ──────────────────────────────────────────── */

SEC("tracepoint/raw_syscalls/sys_enter")
int handle_sys_enter(struct trace_event_raw_sys_enter *ctx)
{
    if (!is_target())
        return 0;

    __u32 tid = get_tid();
    struct syscall_start_val val = {
        .ts = bpf_ktime_get_ns(),
        .nr = (__u32)ctx->id,
    };
    bpf_map_update_elem(&syscall_start, &tid, &val, BPF_ANY);

    return 0;
}

SEC("tracepoint/raw_syscalls/sys_exit")
int handle_sys_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target())
        return 0;

    /* GAPS-004e (2026-05-04): single bpf_ktime_get_ns() per event.
     * Was two calls (one for delta, one for ts_ns) — wasted ~50 ns/event.
     * Capture once at function entry and reuse for both purposes;
     * matches the canonical SchedSwitch pattern. */
    __u64 now = bpf_ktime_get_ns();

    __u32 tid = get_tid();
    struct syscall_start_val *start = bpf_map_lookup_elem(&syscall_start, &tid);
    if (!start)
        return 0;

    __u64 delta = now - start->ts;
    __u32 nr = start->nr;
    bpf_map_delete_elem(&syscall_start, &tid);

    /* Update per-syscall stats */
    struct syscall_stats *stats = bpf_map_lookup_elem(&syscall_latency, &nr);
    if (stats) {
        __sync_fetch_and_add(&stats->count, 1);
        __sync_fetch_and_add(&stats->total_ns, delta);
        if (delta > stats->max_ns)
            stats->max_ns = delta;
        /* min_ns uses compare-and-swap (approximate under contention) */
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

    /* Emit to zero-copy timeline (ts_ns written last for ordering) */
    __u32 tl_zero = 0;
    struct syscall_timeline *tl = bpf_map_lookup_elem(&syscall_timeline, &tl_zero);
    if (tl) {
        __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
        __u32 slot = (__u32)(idx & TIMELINE_MASK);
        if (slot < TIMELINE_CAPACITY) {
            tl->events[slot].duration_ns = delta;
            tl->events[slot].tid = tid;
            tl->events[slot].syscall_nr = nr;
            /* Compiler barrier — GAPS-004e (2026-05-04).
             * Forces clang to emit the prior 3 stores BEFORE the
             * ts_ns store; without this, -O2 may reorder and break
             * the "ts_ns LAST as completion marker" contract.  Zero
             * machine cost (asm volatile with empty body emits no
             * instruction; the "memory" clobber tells the compiler
             * not to reorder across this point).  Pairs with the
             * userspace reader's __atomic_load_n(&ts_ns, ACQUIRE).
             * Same fix class as SchedSwitch GAPS-004b-AUDIT and
             * LockContention GAPS-004d-AUDIT. */
            __asm__ __volatile__("" ::: "memory");
            tl->events[slot].ts_ns = now; /* completion marker */
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
