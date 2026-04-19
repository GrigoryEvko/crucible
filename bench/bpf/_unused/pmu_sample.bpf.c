/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * pmu_sample.bpf.c — Zero-copy PMU sample capture via BPF perf_event programs.
 *
 * Attaches to hardware PMU counter overflows (LLC miss, branch miss, DTLB miss).
 * On every overflow, captures the instruction pointer and writes it to a shared
 * BPF_F_MMAPABLE circular buffer. Userspace reads via volatile mmap — no syscalls.
 *
 * This replaces the perf_event ring buffer path for rare events where period=1
 * captures literally every single hardware event with zero statistical compromise.
 *
 * Three entry points, one shared buffer:
 *   pmu_llc    (event_type=2) — Last-Level Cache miss
 *   pmu_branch (event_type=3) — Branch misprediction
 *   pmu_dtlb   (event_type=4) — Data TLB miss
 *
 * Cycles (event_type=0) and L1D misses (event_type=1) stay on the perf_event2
 * ring buffer path — they're too frequent for BPF overhead.
 *
 * Architecture support: x86_64 and aarch64 (ARMv8.0+, Cortex-A53/A72+).
 */

#include "common.h"

/* ─── Architecture-specific instruction pointer access ───────────────── */

/*
 * bpf_perf_event_data.regs is bpf_user_pt_regs_t:
 *   x86_64:  struct pt_regs       → .ip field
 *   aarch64: struct user_pt_regs  → .pc field
 *
 * Kernel virtual address space boundary for filtering kernel IPs:
 *   x86_64:  canonical high half ≥ 0xFFFF800000000000
 *   aarch64: TTBR1 range          ≥ 0xFFFF000000000000 (48-bit VA)
 */
#if defined(__TARGET_ARCH_arm64)
#define SAMPLE_IP(ctx)       ((ctx)->regs.pc)
#define KERNEL_ADDR_MIN      0xFFFF000000000000ULL
#elif defined(__TARGET_ARCH_x86)
#define SAMPLE_IP(ctx)       ((ctx)->regs.ip)
#define KERNEL_ADDR_MIN      0xFFFF800000000000ULL
#else
#error "Unsupported architecture for PMU sampling"
#endif

/* ─── Shared mmapable circular buffer for all PMU sample events ──────── */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct pmu_sample_timeline);
} pmu_sample_buf SEC(".maps");

/* ─── Common emission logic (inlined into each entry point) ──────────── */

/*
 * Write one PMU sample to the circular buffer.
 *
 * Protocol (identical to timeline events):
 * 1. Atomic write_idx increment claims a slot
 * 2. Payload fields written first (ip, tid, event_type)
 * 3. ts_ns written LAST — non-zero signals completion to reader
 *
 * Runs in NMI/PMI context — only BPF helpers safe in that context are used.
 *
 * Memory ordering:
 *   x86_64:  TSO guarantees store order — if reader sees ts_ns, all prior stores visible.
 *   aarch64: The __sync_fetch_and_add (LDAXR/STLXR) provides a full barrier before
 *            the payload stores. All fields within the 24-byte event struct share the
 *            same cache line — cache coherency delivers the full line atomically to
 *            the reader. Rust volatile loads provide the compiler barrier.
 *            Tested on: Cortex-A53 (RPi3), Cortex-A72 (RPi4), Neoverse N1 (Graviton2).
 */
static __always_inline int emit_pmu_sample(struct bpf_perf_event_data *ctx, __u8 etype)
{
    /* Filter: only our process */
    if (!is_target())
        return 0;

    /* Instruction pointer from the interrupted context (arch-portable) */
    __u64 ip = SAMPLE_IP(ctx);

    /* Skip kernel-space IPs */
    if (ip >= KERNEL_ADDR_MIN)
        return 0;

    /* Look up the shared circular buffer */
    __u32 zero = 0;
    struct pmu_sample_timeline *tl = bpf_map_lookup_elem(&pmu_sample_buf, &zero);
    if (!tl)
        return 0;

    /* Atomically claim a slot */
    __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
    __u32 slot = (__u32)(idx & PMU_SAMPLE_MASK);

    /* Bounds check satisfies verifier (always true due to mask) */
    if (slot < PMU_SAMPLE_CAPACITY) {
        tl->events[slot].ip = ip;
        tl->events[slot].tid = get_tid();
        tl->events[slot].event_type = etype;
        /* ts_ns LAST — completion marker */
        tl->events[slot].ts_ns = bpf_ktime_get_ns();
    }

    return 0;
}

/* ─── Entry points (one per PMU event type) ──────────────────────────── */

SEC("perf_event")
int pmu_llc(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 2);  /* LLC miss */
}

SEC("perf_event")
int pmu_branch(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 3);  /* Branch miss */
}

SEC("perf_event")
int pmu_dtlb(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 4);  /* DTLB miss */
}

/*
 * AMD IBS (Instruction-Based Sampling) entry points.
 *
 * IBS provides PRECISE instruction pointers — zero skid, unlike generic PMU
 * counters where the captured IP can drift several instructions past the actual
 * event. The kernel's IBS handler reads IBS_OP_RIP MSR and places it in
 * pt_regs.ip, so our existing SAMPLE_IP(ctx) macro works unchanged.
 *
 * IBS-Op samples completed micro-ops. Each sample carries the exact instruction
 * that was executing when the counter overflowed. The hardware also captures
 * data address, cache level, load latency, and TLB info in MSRs — but those
 * aren't exposed through bpf_perf_event_data, so we capture IP only here.
 * Full IBS data (data_src, latency) can be added via perf_event ring path later.
 *
 * IBS-Fetch samples instruction fetches. Provides I-cache and I-TLB info.
 * Less useful for our use case (memory/compute profiling) but included for
 * completeness and front-end stall analysis.
 *
 * Detection is runtime — the controller reads /sys/bus/event_source/devices/
 * ibs_op/type to get the dynamic PMU type ID. Non-AMD systems simply skip.
 */
SEC("perf_event")
int pmu_ibs_op(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 5);  /* IBS-Op: precise micro-op sample */
}

SEC("perf_event")
int pmu_ibs_fetch(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 6);  /* IBS-Fetch: instruction fetch sample */
}

/*
 * Software event entry points.
 *
 * Attached via PerfEventConfig::Software — fire on kernel software events.
 * ctx->regs.ip gives the instruction pointer at the point the event occurred:
 *   - Major page fault: IP of the faulting load/store (memory-mapping hotspot)
 *   - CPU migration: IP where the task was running when moved to another core
 *   - Alignment fault: IP of the misaligned memory access
 */
SEC("perf_event")
int pmu_sw_pagefault_maj(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 7);  /* Major page fault */
}

SEC("perf_event")
int pmu_sw_cpu_migration(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 8);  /* CPU migration */
}

SEC("perf_event")
int pmu_sw_alignment_fault(struct bpf_perf_event_data *ctx)
{
    return emit_pmu_sample(ctx, 9);  /* Alignment fault */
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
