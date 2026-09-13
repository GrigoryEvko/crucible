/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef __CRUCIBLE_PERF_BPF_COMMON_H
#define __CRUCIBLE_PERF_BPF_COMMON_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* Userspace rewrites this in .rodata before the program is loaded. */
const volatile __u32 target_tgid = 0;

#define MAX_STACK_DEPTH 127
#define MAX_ENTRIES 65536
#define MAX_STACKS 16384

struct offcpu_key {
    __s32 stack_id;
    __u32 tid;
};

struct offcpu_val {
    __u64 total_ns;
    __u64 count;
    __u64 max_ns;
};

struct syscall_stats {
    __u64 count;
    __u64 total_ns;
    __u64 max_ns;
    __u64 min_ns;
};

struct lock_key {
    __u64 addr; /* futex address in the user address space */
    __s32 stack_id;
    __u32 _pad;
};

struct lock_val {
    __u64 total_wait_ns;
    __u64 count;
    __u64 max_wait_ns;
};

struct fault_key {
    __s32 stack_id;
    __u32 tid;
};

struct fault_val {
    __u64 count;
    __u64 major_count;
};

#ifndef BPF_F_MMAPABLE
#define BPF_F_MMAPABLE (1U << 10)
#endif

#define TIMELINE_CAPACITY 4096
#define TIMELINE_MASK (TIMELINE_CAPACITY - 1)

/*
 * The writer stores ts_ns last, and a non-zero ts_ns is what marks a record
 * complete. The reader tests ts_ns before it trusts any other field.
 * On x86-64 the store order follows from TSO, so a visible ts_ns implies the
 * earlier stores are visible. On aarch64 the LDAXR and STLXR pair behind
 * __sync_fetch_and_add is a full barrier ahead of the field stores, and all
 * fields sit in one cache line, which coherency delivers as a unit.
 */
struct timeline_sched_event {
    __u64 off_cpu_ns;
    __u32 tid;
    __u32 on_cpu; /* CPU core switched onto */
    __u64 ts_ns; /* written last */
    __u64 _pad; /* Rounds the record to 32 bytes, which divides
                        * the cache line, so no slot straddles two
                        * lines.  At 24 bytes a slot could deliver a
                        * visible ts_ns on one line while the other
                        * fields sit stale on the next. */
};

struct timeline_syscall_event {
    __u64 duration_ns;
    __u32 tid;
    __u32 syscall_nr;
    __u64 ts_ns; /* written last */
    __u64 _pad; /* Rounds the record to 32 bytes so no slot
                        * straddles a cache line.  Refer to
                        * timeline_sched_event. */
};

struct timeline_lock_event {
    __u64 futex_addr;
    __u64 wait_ns;
    __u32 tid;
    __u32 _pad;
    __u64 ts_ns; /* written last */
};

/*
 * write_idx counts every record ever published and never resets. The slot is
 * write_idx masked to the capacity. The header fills a whole cache line so
 * that the events array starts cache-aligned.
 */
struct timeline_header {
    __u64 write_idx;
    __u64 _pad[7];
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

#define PMU_SAMPLE_CAPACITY 32768
#define PMU_SAMPLE_MASK (PMU_SAMPLE_CAPACITY - 1)

/*
 * One record per hardware counter overflow. event_type is 0 for cycles,
 * 1 for L1D miss, 2 for LLC miss, 3 for branch miss, 4 for DTLB miss,
 * 5 for AMD IBS-Op, 6 for AMD IBS-Fetch, 7 for major page fault,
 * 8 for CPU migration and 9 for alignment fault.
 */
struct pmu_sample_event {
    __u64 ip; /* instruction pointer in the user address space */
    __u32 tid;
    __u8 event_type;
    __u8 _pad[3];
    __u64 ts_ns; /* written last */
    __u64 _pad8; /* Rounds the record to 32 bytes so no slot
                         * straddles a cache line.  Refer to
                         * timeline_sched_event. */
};

struct pmu_sample_timeline {
    struct timeline_header hdr;
    struct pmu_sample_event events[PMU_SAMPLE_CAPACITY];
};

/*
 * The target_tgid != 0 test rejects the case where the userspace .rodata
 * rewrite never happened. Without it the idle task, whose tgid is 0, matches
 * every call and fills the downstream maps with swapper records.
 */
static __always_inline bool is_target(void) {
    __u32 tgid = bpf_get_current_pid_tgid() >> 32;
    return target_tgid != 0 && tgid == target_tgid;
}

static __always_inline __u32 get_tid(void) { return (__u32)bpf_get_current_pid_tgid(); }

#endif /* __CRUCIBLE_PERF_BPF_COMMON_H */
