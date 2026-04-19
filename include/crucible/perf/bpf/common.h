/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Shared definitions for the Crucible perf BPF programs (sched_switch,
 * syscall_latency, lock_contention, pmu_sample). Ported 1:1 from
 * symbiotic's bpf/common.h. */

#ifndef __CRUCIBLE_PERF_BPF_COMMON_H
#define __CRUCIBLE_PERF_BPF_COMMON_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* ─── Target PID filter (set from userspace via .rodata rewrite) ──────── */

const volatile __u32 target_tgid = 0;

/* ─── Shared constants ────────────────────────────────────────────────── */

#define MAX_STACK_DEPTH  127
#define MAX_ENTRIES      65536
#define MAX_STACKS       16384

/* ─── Off-CPU types (shared between BPF and userspace) ───────────────── */

struct offcpu_key {
    __s32 stack_id;
    __u32 tid;
};

struct offcpu_val {
    __u64 total_ns;
    __u64 count;
    __u64 max_ns;
};

/* ─── Syscall latency types ──────────────────────────────────────────── */

struct syscall_stats {
    __u64 count;
    __u64 total_ns;
    __u64 max_ns;
    __u64 min_ns;
};

/* ─── Lock contention types ──────────────────────────────────────────── */

struct lock_key {
    __u64 addr;      /* futex address (userspace VA) */
    __s32 stack_id;  /* stack trace at contention point */
    __u32 _pad;
};

struct lock_val {
    __u64 total_wait_ns;
    __u64 count;
    __u64 max_wait_ns;
};

/* ─── Page fault types ───────────────────────────────────────────────── */

struct fault_key {
    __s32 stack_id;
    __u32 tid;
};

struct fault_val {
    __u64 count;
    __u64 major_count;
};

/* ─── BPF_F_MMAPABLE for zero-copy shared memory ─────────────────────── */

#ifndef BPF_F_MMAPABLE
#define BPF_F_MMAPABLE (1U << 10)
#endif

/* ─── Timeline circular buffer (mmap'd zero-copy, sub-ns reads) ──────── */

#define TIMELINE_CAPACITY 4096          /* events per category, power of 2 */
#define TIMELINE_MASK     (TIMELINE_CAPACITY - 1)

/*
 * Per-event records. Write ts_ns LAST — non-zero signals completion.
 * Reader checks ts_ns != 0 before trusting other fields.
 * x86_64: TSO guarantees store order — ts_ns visible implies prior stores visible.
 * aarch64: __sync_fetch_and_add (LDAXR/STLXR) provides full barrier before stores;
 *          all fields share the same cache line, delivered atomically by coherency.
 */
struct timeline_sched_event {
    __u64 off_cpu_ns;  /* how long thread was off-CPU */
    __u32 tid;
    __u32 on_cpu;      /* CPU core switched onto */
    __u64 ts_ns;       /* bpf_ktime_get_ns() — WRITTEN LAST */
};

struct timeline_syscall_event {
    __u64 duration_ns;
    __u32 tid;
    __u32 syscall_nr;
    __u64 ts_ns;       /* bpf_ktime_get_ns() — WRITTEN LAST */
};

struct timeline_lock_event {
    __u64 futex_addr;
    __u64 wait_ns;
    __u32 tid;
    __u32 _pad;
    __u64 ts_ns;       /* bpf_ktime_get_ns() — WRITTEN LAST */
};

/*
 * Circular buffer header: write_idx is atomically incremented.
 * Slot = write_idx & TIMELINE_MASK.
 * Padded to 64 bytes (one cache line) so events start cache-aligned.
 */
struct timeline_header {
    __u64 write_idx;   /* monotonically increasing, never resets */
    __u64 _pad[7];     /* pad to 64 bytes */
};

struct sched_timeline {
    struct timeline_header hdr;
    struct timeline_sched_event events[TIMELINE_CAPACITY];
};

struct syscall_timeline {
    struct timeline_header hdr;
    struct timeline_syscall_event events[TIMELINE_CAPACITY];
};

struct lock_timeline {
    struct timeline_header hdr;
    struct timeline_lock_event events[TIMELINE_CAPACITY];
};

/* ─── PMU sample circular buffer (BPF perf_event → mmap'd zero-copy) ─── */

#define PMU_SAMPLE_CAPACITY 32768       /* 2^15 events, larger than timeline */
#define PMU_SAMPLE_MASK     (PMU_SAMPLE_CAPACITY - 1)

/*
 * PMU sample event — one per hardware counter overflow.
 * event_type: 0=cycles, 1=L1D-miss, 2=LLC-miss, 3=branch-miss, 4=DTLB-miss,
 *             5=IBS-Op (AMD precise micro-op), 6=IBS-Fetch (AMD instruction fetch),
 *             7=major-pagefault, 8=cpu-migration, 9=alignment-fault
 * Write ts_ns LAST as completion marker.
 */
struct pmu_sample_event {
    __u64 ip;           /* instruction pointer (userspace virtual addr) */
    __u32 tid;          /* thread ID */
    __u8  event_type;   /* PMU event discriminator */
    __u8  _pad[3];      /* align to 8 bytes */
    __u64 ts_ns;        /* bpf_ktime_get_ns() — WRITTEN LAST */
};

struct pmu_sample_timeline {
    struct timeline_header hdr;
    struct pmu_sample_event events[PMU_SAMPLE_CAPACITY];
};

/* ─── Helpers: target filter + tid extraction ────────────────────────── */

static __always_inline bool is_target(void)
{
    __u32 tgid = bpf_get_current_pid_tgid() >> 32;
    return tgid == target_tgid;
}

static __always_inline __u32 get_tid(void)
{
    return (__u32)bpf_get_current_pid_tgid();
}

#endif /* __CRUCIBLE_PERF_BPF_COMMON_H */
