/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * drm_fences.bpf.c — generic CPU↔GPU dma_fence synchronization events.
 *
 * STATUS: doc-only stub.  Tier-2 audit-round-2 addition.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * dma_fence is the kernel's vendor-agnostic CPU↔GPU sync primitive.
 * Every cross-device wait (CUDA stream sync, ROCm event wait, AF_XDP
 * NIC↔GPU coordination) bottoms out in a dma_fence.  Per-fence wait
 * duration tells you when the CPU is blocked waiting on a device —
 * usually a sign of inadequate pipelining or a producer-side stall.
 *
 * Vendor SDKs expose this WITHIN their stack; dma_fence catches the
 * cross-vendor / cross-driver / DMA-buf-sharing cases the SDKs miss.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: tracepoint
 * Attachment points:
 *   - tracepoint/dma_fence/dma_fence_emit         — fence published
 *   - tracepoint/dma_fence/dma_fence_init         — fence created
 *   - tracepoint/dma_fence/dma_fence_signaled     — fence completed
 *   - tracepoint/dma_fence/dma_fence_wait_start   — wait began
 *   - tracepoint/dma_fence/dma_fence_wait_end     — wait completed
 *   - tracepoint/dma_fence/dma_fence_destroy      — refcount → 0
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_driver: HASH[driver_name[16] → wait histogram]
 * - per_context: LRU_HASH[(ctx_id, seqno) → emit_ts]
 * - timeline: ARRAY[1] + BPF_F_MMAPABLE
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50-100 ns.  Event rate: 100-10K/sec depending on
 * GPU/NIC traffic.  Default-on for GPU-bearing deployments.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - Vendor-internal fences may not call the generic dma_fence path
 *   (NVIDIA proprietary scheduler bypass; ROCm has its own as well).
 * - Cross-driver fences (DMA-buf imports between vendors) are the
 *   most useful case; intra-driver duplicates vendor SDK coverage.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Vendor counterparts: NVML stream-sync events, ROCm SMI dispatch
 *   events.  This sees the kernel-side; vendor SDK sees user-side.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
