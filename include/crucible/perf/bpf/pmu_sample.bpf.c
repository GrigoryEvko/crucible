/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

/*
 * KERNEL_ADDR_MIN is where the kernel half of the address space starts: the
 * canonical high half on x86-64, and the TTBR1 range on aarch64 with a 48-bit
 * virtual address. An address at or above it is a kernel address.
 */
#if defined(__TARGET_ARCH_arm64)
#define SAMPLE_IP(ctx) ((ctx)->regs.pc)
#define KERNEL_ADDR_MIN 0xFFFF000000000000ULL
#elif defined(__TARGET_ARCH_x86)
#define SAMPLE_IP(ctx) ((ctx)->regs.ip)
#define KERNEL_ADDR_MIN 0xFFFF800000000000ULL
#else
#error "Unsupported architecture for PMU sampling"
#endif

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct pmu_sample_timeline);
} pmu_sample_buf SEC(".maps");

/*
 * This runs in NMI context, so it may call only the BPF helpers that are safe
 * to call there.
 */
static __always_inline int emit_pmu_sample(struct bpf_perf_event_data* ctx, __u8 etype) {
    if (!is_target()) return 0;

    __u64 ip = SAMPLE_IP(ctx);

    if (ip >= KERNEL_ADDR_MIN) return 0;

    __u32 zero = 0;
    struct pmu_sample_timeline* tl = bpf_map_lookup_elem(&pmu_sample_buf, &zero);
    if (!tl) return 0;

    __u64 idx = __sync_fetch_and_add(&tl->hdr.write_idx, 1);
    __u32 slot = (__u32)(idx & PMU_SAMPLE_MASK);

    /* The mask already bounds slot. The verifier does not follow that, and
     * rejects the array access without this test. */
    if (slot < PMU_SAMPLE_CAPACITY) {
        tl->events[slot].ip = ip;
        tl->events[slot].tid = get_tid();
        tl->events[slot].event_type = etype;
        /* The memory clobber stops the compiler from sinking the three stores
         * above past the ts_ns store. It pairs with the acquire load of ts_ns
         * in the reader. */
        __asm__ __volatile__("" ::: "memory");
        /* completion marker */
        tl->events[slot].ts_ns = bpf_ktime_get_ns();
    }

    return 0;
}

SEC("perf_event")
int pmu_llc(struct bpf_perf_event_data* ctx) { return emit_pmu_sample(ctx, 2); /* LLC miss */ }

SEC("perf_event")
int pmu_branch(struct bpf_perf_event_data* ctx) { return emit_pmu_sample(ctx, 3); /* Branch miss */ }

SEC("perf_event")
int pmu_dtlb(struct bpf_perf_event_data* ctx) { return emit_pmu_sample(ctx, 4); /* DTLB miss */ }

/*
 * AMD Instruction-Based Sampling reports the exact instruction rather than one
 * several past the event. The kernel IBS handler copies the IBS_OP_RIP model
 * specific register into pt_regs, so SAMPLE_IP reads it unchanged. The wider
 * IBS payload of data address, cache level, load latency and TLB state stays
 * in model specific registers that bpf_perf_event_data does not expose.
 *
 * The IBS PMU type identifier is allocated at run time and read from sysfs, so
 * a machine without IBS simply never attaches these two programs.
 */
SEC("perf_event")
int pmu_ibs_op(struct bpf_perf_event_data* ctx) {
    return emit_pmu_sample(ctx, 5); /* IBS-Op: precise micro-op sample */
}

SEC("perf_event")
int pmu_ibs_fetch(struct bpf_perf_event_data* ctx) {
    return emit_pmu_sample(ctx, 6); /* IBS-Fetch: instruction fetch sample */
}

/*
 * These three attach to kernel software events rather than to hardware
 * counters. The instruction pointer means the faulting load or store for a
 * major page fault, the place the task was running when it moved for a CPU
 * migration, and the misaligned access for an alignment fault.
 */
SEC("perf_event")
int pmu_sw_pagefault_maj(struct bpf_perf_event_data* ctx) { return emit_pmu_sample(ctx, 7); /* Major page fault */ }

SEC("perf_event")
int pmu_sw_cpu_migration(struct bpf_perf_event_data* ctx) { return emit_pmu_sample(ctx, 8); /* CPU migration */ }

SEC("perf_event")
int pmu_sw_alignment_fault(struct bpf_perf_event_data* ctx) { return emit_pmu_sample(ctx, 9); /* Alignment fault */ }

char LICENSE[] SEC("license") = "Dual BSD/GPL";
