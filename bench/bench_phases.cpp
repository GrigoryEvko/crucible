// Phase breakdown benchmark for build_trace pipeline.
//
// Two layers of output:
//   • Top-level Report (BPF-sensed, p50/p99/p99.9): full build_trace
//     pipeline on a recorded .crtrace file. Standard bench harness
//     output with cache / fault / freq-drift sensory data.
//   • Phase-by-phase table: RDTSC-bracketed measurement of each phase
//     run N times. Custom output format — the whole point is per-phase
//     attribution that the standard harness cannot express.
//
// Usage:
//   ./build-bench/bench/bench_phases bench/vit_b.crtrace [iters]
//
// Phases measured:
//   P0  Pre-scan       count inputs/outputs/scalars, check MetaLog
//   P1  Alloc+init     arena allocs, PtrMap gen bump, memset scratch
//   P2a Copy fields    schema/shape/scope/callsite + meta ptrs + aux ptrs
//   P2b Content hash   wymix over all tensor dims for streaming hash
//   P2c PtrMap lookup  DFG edge building (input ptr_map_lookup)
//   P2d PtrMap insert  output slot tracking (ptr_map_insert + aliases)
//   P3  Slot copy      scratch SlotInfo → arena TensorSlot
//   P4  build_csr      CSR property graph construction
//   P5  memory_plan    counting sort + sweep-line offset assignment
//
// The Phase 2 sub-parts (P2a–P2d) are measured by running the main loop
// 4 times, each time executing only one sub-part. Cache state differs
// slightly from the fused loop, but the relative ratios are accurate.

#include "bench_harness.h"

#include <crucible/Arena.h>
#include <crucible/BackgroundThread.h>
#include <crucible/Effects.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceLoader.h>
#include <crucible/TraceRing.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace crucible;

namespace {

// Benches run outside the normal bg thread so we spin a standalone
// fx::Bg context once and thread its .alloc token through every
// capability-tagged call.
const fx::Bg BG_CTX;
constexpr auto A = BG_CTX.alloc;

struct PhaseTiming {
    const char* name;
    double      total_ns;  // sum across all iterations
    uint64_t    count;     // number of iterations
};

void print_phase_table(PhaseTiming* phases, uint32_t n,
                       double total_ns, uint64_t iters) {
    std::printf("\n  %-22s  %8s  %8s  %5s\n",
                "Phase", "ns/op", "total ms", "  %");
    std::printf("  %-22s  %8s  %8s  %5s\n",
                "──────────────────────", "────────", "────────", "─────");

    for (uint32_t i = 0; i < n; i++) {
        const double ns_per = phases[i].total_ns
                            / static_cast<double>(iters);
        const double pct = 100.0 * phases[i].total_ns / total_ns;
        std::printf("  %-22s  %8.1f  %8.2f  %5.1f%%\n",
                    phases[i].name, ns_per,
                    phases[i].total_ns / 1e6, pct);
    }

    double sum_ns = 0;
    for (uint32_t i = 0; i < n; i++) sum_ns += phases[i].total_ns;
    const double ns_per  = total_ns / static_cast<double>(iters);
    const double sum_per = sum_ns   / static_cast<double>(iters);

    std::printf("  %-22s  %8s  %8s  %5s\n",
                "──────────────────────", "────────", "────────", "─────");
    std::printf("  %-22s  %8.1f  %8.2f  %5.1f%%\n",
                "SUM(phases)", sum_per, sum_ns / 1e6,
                100.0 * sum_ns / total_ns);
    std::printf("  %-22s  %8.1f  %8.2f  100.0%%\n",
                "TOTAL (measured)", ns_per, total_ns / 1e6);
}

// ── Top-level Report via bench::run ──────────────────────────────────
//
// The full build_trace pipeline on a real recorded trace. The body
// resets the arena and MetaLog from scratch every call, then runs
// build_trace. Scope / callsite / meta cursors are restored to their
// recorded values each sample so every sample is independent and
// measures the same work.
bench::Report run_fullpipeline(BackgroundThread& bg, MetaLog& meta_log,
                               const LoadedTrace& trace) {
    const size_t arena_bytes = std::max(size_t{1} << 20,
        static_cast<size_t>(trace.num_ops) * 256);

    auto repopulate = [&]() {
        meta_log.reset();
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < trace.num_ops; i++) {
            const uint16_t n = trace.entries[i].num_inputs
                             + trace.entries[i].num_outputs;
            if (n > 0 && cursor + n <= trace.num_metas) {
                (void)meta_log.try_append(&trace.metas[cursor], n);
                cursor += n;
            }
        }
    };

    char label[64];
    std::snprintf(label, sizeof(label),
                  "build_trace (full pipeline, %u ops)", trace.num_ops);

    bench::Run r{label};
    if (const int c = bench::env_core(); c >= 0) (void)r.core(c);
    return r.samples(500).warmup(10).batch(1).measure([&]{
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();
        auto* graph = bg.build_trace(A, trace.num_ops);
        bench::do_not_optimize(graph);
    });
}

// ── Phase tables via manual rdtsc bracketing ─────────────────────────
//
// This section does NOT use bench::run — the phase-level timings are
// the whole point, and the standard harness measures entire bodies.
// Each phase's rdtsc_start/end pair reads the TSC with LFENCE bracketing
// (Intel "How to Benchmark..." 2010 §3.2.1, via bench::rdtsc_start/end).

void bench_phases_toplevel(
    BackgroundThread& bg,
    MetaLog&          meta_log,
    const LoadedTrace& trace,
    uint32_t          iters)
{
    std::printf("\n=== Top-level phase breakdown (%u ops, %u iters) ===\n",
                trace.num_ops, iters);

    const size_t arena_bytes = std::max(size_t{1} << 20,
        static_cast<size_t>(trace.num_ops) * 256);
    const double nspc = bench::Timer::ns_per_cycle();

    auto repopulate = [&]() {
        meta_log.reset();
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < trace.num_ops; i++) {
            const uint16_t n = trace.entries[i].num_inputs
                             + trace.entries[i].num_outputs;
            if (n > 0 && cursor + n <= trace.num_metas) {
                (void)meta_log.try_append(&trace.metas[cursor], n);
                cursor += n;
            }
        }
    };

    // Warmup: 5 full pipeline runs to hit the cache.
    for (uint32_t w = 0; w < 5; w++) {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();
        auto* g = bg.build_trace(A, trace.num_ops);
        bench::do_not_optimize(g);
    }

    // Measurement: run build_trace N times, accumulate.
    double              total_ns = 0;
    std::vector<double> samples(iters);

    for (uint32_t iter = 0; iter < iters; iter++) {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();

        const uint64_t t0 = bench::rdtsc_start();
        auto* graph = bg.build_trace(A, trace.num_ops);
        const uint64_t t1 = bench::rdtsc_end();

        bench::do_not_optimize(graph);
        const double ns = static_cast<double>(t1 - t0) * nspc;
        total_ns += ns;
        samples[iter] = ns;
    }

    std::sort(samples.begin(), samples.end());
    std::printf("  full pipeline: min=%.1f  med=%.1f  max=%.1f ns/op\n",
                samples.front(),
                samples[samples.size() / 2],
                samples.back());

    // Isolated build_csr + compute_memory_plan.
    double ns_csr = 0;
    double ns_memplan = 0;
    {
        // One full build_trace to get realistic edge data.
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();
        auto* ref = bg.build_trace(A, trace.num_ops);

        const uint32_t num_ops   = ref->num_ops;
        const uint32_t num_slots = ref->num_slots;
        const uint32_t num_edges = ref->fwd_offsets[num_ops];

        std::vector<Edge> saved_edges(num_edges);
        for (uint32_t i = 0; i < num_ops; i++) {
            for (uint32_t e = ref->fwd_offsets[i];
                 e < ref->fwd_offsets[i + 1]; e++) {
                saved_edges[e] = ref->fwd_edges[e];
            }
        }

        std::vector<TensorSlot> saved_slots(ref->slots,
                                             ref->slots + num_slots);

        // build_csr
        for (uint32_t iter = 0; iter < iters; iter++) {
            Arena csr_arena{1 << 18};
            auto* graph2 = csr_arena.alloc_obj<TraceGraph>(A);
            graph2->ops     = nullptr;
            graph2->num_ops = num_ops;

            const uint64_t t0 = bench::rdtsc_start();
            build_csr(A, csr_arena, graph2, saved_edges.data(),
                      static_cast<uint32_t>(saved_edges.size()), num_ops);
            const uint64_t t1 = bench::rdtsc_end();

            bench::do_not_optimize(graph2);
            ns_csr += static_cast<double>(t1 - t0) * nspc;
        }

        // compute_memory_plan
        for (uint32_t iter = 0; iter < iters; iter++) {
            bg.arena.~Arena();
            new (&bg.arena) Arena{arena_bytes};
            auto* s = bg.arena.alloc_array<TensorSlot>(A, num_slots);
            std::memcpy(s, saved_slots.data(),
                        num_slots * sizeof(TensorSlot));

            const uint64_t t0 = bench::rdtsc_start();
            auto* plan = bg.compute_memory_plan(A, s, num_slots);
            const uint64_t t1 = bench::rdtsc_end();

            bench::do_not_optimize(plan);
            ns_memplan += static_cast<double>(t1 - t0) * nspc;
        }

        const double ns_other = total_ns - ns_csr - ns_memplan;

        PhaseTiming phases[] = {
            {"Main loop (P0-P3)", ns_other,   iters},
            {"build_csr (P4)",    ns_csr,     iters},
            {"memory_plan (P5)",  ns_memplan, iters},
        };
        print_phase_table(phases, 3, total_ns, iters);
    }
}

void bench_phase2_subparts(
    BackgroundThread& bg,
    MetaLog&          meta_log,
    const LoadedTrace& trace,
    uint32_t          iters)
{
    std::printf("\n=== Phase 2 sub-part breakdown (%u ops, %u iters) ===\n",
                trace.num_ops, iters);

    const size_t arena_bytes = std::max(size_t{1} << 20,
        static_cast<size_t>(trace.num_ops) * 256);
    const uint32_t count = trace.num_ops;
    const double nspc = bench::Timer::ns_per_cycle();

    uint32_t trace_total_inputs = 0, trace_total_outputs = 0;
    for (uint32_t i = 0; i < count; i++) {
        trace_total_inputs  += trace.entries[i].num_inputs;
        trace_total_outputs += trace.entries[i].num_outputs;
    }
    (void)trace_total_inputs;
    (void)trace_total_outputs;

    auto repopulate = [&]() {
        meta_log.reset();
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < trace.num_ops; i++) {
            const uint16_t n = trace.entries[i].num_inputs
                             + trace.entries[i].num_outputs;
            if (n > 0 && cursor + n <= trace.num_metas) {
                (void)meta_log.try_append(&trace.metas[cursor], n);
                cursor += n;
            }
        }
    };

    // One build_trace to prime scratch buffers and reference ops.
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                  trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                   trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                      trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();
    auto* ref = bg.build_trace(A, count);
    (void)ref;

    // ── P0: Pre-scan ──────────────────────────────────────────────
    double ns_p0 = 0;
    for (uint32_t iter = 0; iter < iters; iter++) {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());

        const TraceRing::Entry* trace_data = bg.current_trace.data();
        const MetaIndex*        meta_data  = bg.current_meta_starts.data();

        const uint64_t t0 = bench::rdtsc_start();

        uint32_t max_meta_end = 0, first_meta = UINT32_MAX;
        uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
        for (uint32_t i = 0; i < count; i++) {
            const MetaIndex ms = meta_data[i];
            const auto&     re = trace_data[i];
            if (ms.is_valid()) {
                if (first_meta == UINT32_MAX) first_meta = ms.raw();
                const uint32_t end = ms.raw() + re.num_inputs + re.num_outputs;
                if (end > max_meta_end) max_meta_end = end;
            }
            total_inputs  += re.num_inputs;
            total_outputs += re.num_outputs;
            total_scalars += std::min(re.num_scalar_args, uint16_t(5));
        }

        const uint64_t t1 = bench::rdtsc_end();

        bench::do_not_optimize(max_meta_end);
        bench::do_not_optimize(total_inputs);
        bench::do_not_optimize(total_outputs);
        bench::do_not_optimize(total_scalars);
        ns_p0 += static_cast<double>(t1 - t0) * nspc;
    }

    // ── P1: Alloc + PtrMap gen + memset ───────────────────────────
    double ns_p1 = 0;
    for (uint32_t iter = 0; iter < iters; iter++) {
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};

        uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
        for (uint32_t i = 0; i < count; i++) {
            total_inputs  += trace.entries[i].num_inputs;
            total_outputs += trace.entries[i].num_outputs;
            total_scalars += std::min(trace.entries[i].num_scalar_args,
                                      uint16_t(5));
        }

        const uint64_t t0 = bench::rdtsc_start();

        auto* ops = bg.arena.alloc_array<TraceEntry>(A, count);
        const size_t aux_bytes =
              static_cast<size_t>(total_scalars) * sizeof(int64_t)
            + static_cast<size_t>(total_inputs)  * sizeof(OpIndex)
            + static_cast<size_t>(total_inputs)  * sizeof(SlotId)
            + static_cast<size_t>(total_outputs) * sizeof(SlotId);
        char* aux = (aux_bytes > 0)
            ? static_cast<char*>(
                  bg.arena.alloc(A,
                      crucible::safety::Positive<size_t>{aux_bytes},
                      crucible::safety::PowerOfTwo<size_t>{alignof(int64_t)}))
            : nullptr;
        bg.ensure_scratch_buffers(total_inputs, total_outputs);
        const uint32_t slot_cap = std::min(bg.slot_cap_max_.get(),
            std::max(uint32_t{256}, total_inputs + total_outputs));
        std::memset(bg.scratch_slots_, 0,
                    slot_cap * sizeof(BackgroundThread::SlotInfo));

        const uint64_t t1 = bench::rdtsc_end();

        bench::do_not_optimize(ops);
        bench::do_not_optimize(aux);
        ns_p1 += static_cast<double>(t1 - t0) * nspc;
    }

    // ── P2a: Copy fields (no hash, no PtrMap) ─────────────────────
    double ns_p2a = 0;
    for (uint32_t iter = 0; iter < iters; iter++) {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();

        const TraceRing::Entry* trace_data    = bg.current_trace.data();
        const MetaIndex*        meta_data     = bg.current_meta_starts.data();
        const ScopeHash*        scope_data    = bg.current_scope_hashes.data();
        const CallsiteHash*     callsite_data = bg.current_callsite_hashes.data();

        uint32_t max_meta_end = 0, first_meta = UINT32_MAX;
        uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
        for (uint32_t i = 0; i < count; i++) {
            const MetaIndex ms = meta_data[i];
            const auto&     re = trace_data[i];
            if (ms.is_valid()) {
                if (first_meta == UINT32_MAX) first_meta = ms.raw();
                const uint32_t end = ms.raw() + re.num_inputs + re.num_outputs;
                if (end > max_meta_end) max_meta_end = end;
            }
            total_inputs  += re.num_inputs;
            total_outputs += re.num_outputs;
            total_scalars += std::min(re.num_scalar_args, uint16_t(5));
        }

        auto* ops = bg.arena.alloc_array<TraceEntry>(A, count);
        const uint32_t total_metas = (first_meta != UINT32_MAX)
            ? max_meta_end - first_meta : 0;
        TensorMeta* meta_base = (total_metas > 0)
            ? meta_log.try_contiguous(first_meta, total_metas) : nullptr;
        if (!meta_base && total_metas > 0) {
            meta_base = bg.arena.alloc_array<TensorMeta>(A, total_metas);
            for (uint32_t m = 0; m < total_metas; m++)
                meta_base[m] = meta_log.at(first_meta + m);
        }
        const size_t aux_bytes =
              static_cast<size_t>(total_scalars) * sizeof(int64_t)
            + static_cast<size_t>(total_inputs)  * sizeof(OpIndex)
            + static_cast<size_t>(total_inputs)  * sizeof(SlotId)
            + static_cast<size_t>(total_outputs) * sizeof(SlotId);
        char* aux_cursor = (aux_bytes > 0)
            ? static_cast<char*>(
                  bg.arena.alloc(A,
                      crucible::safety::Positive<size_t>{aux_bytes},
                      crucible::safety::PowerOfTwo<size_t>{alignof(int64_t)}))
            : nullptr;

        const uint64_t t0 = bench::rdtsc_start();

        for (uint32_t i = 0; i < count; i++) {
            const auto&     re = trace_data[i];
            const MetaIndex ms = meta_data[i];
            auto&           te = ops[i];

            te.schema_hash   = re.schema_hash;
            te.shape_hash    = re.shape_hash;
            te.scope_hash    = scope_data[i];
            te.callsite_hash = callsite_data[i];
            te.num_inputs    = re.num_inputs;
            te.num_outputs   = re.num_outputs;
            te.grad_enabled  = re.grad_enabled();

            const uint8_t flags = re.op_flags;
            te.inference_mode = (flags & op_flag::INFERENCE_MODE) != 0;
            te.is_mutable     = (flags & op_flag::IS_MUTABLE)     != 0;
            te.training_phase = static_cast<TrainingPhase>(
                (flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
            te.torch_function = (flags & op_flag::TORCH_FUNCTION) != 0;
            te.kernel_id = CKernelId::OPAQUE;
            const uint16_t n_scalars =
                std::min(re.num_scalar_args, uint16_t(5));
            te.num_scalar_args = n_scalars;

            if (ms.is_valid()) {
                const uint16_t n_in        = re.num_inputs;
                const uint16_t n_out       = re.num_outputs;
                const uint32_t meta_offset = ms.raw() - first_meta;
                te.input_metas  = meta_base + meta_offset;
                te.output_metas = meta_base + meta_offset + n_in;
                te.scalar_args  = (n_scalars > 0)
                    ? reinterpret_cast<int64_t*>(aux_cursor) : nullptr;
                aux_cursor += n_scalars * sizeof(int64_t);
                te.input_trace_indices =
                    reinterpret_cast<OpIndex*>(aux_cursor);
                aux_cursor += n_in * sizeof(OpIndex);
                te.input_slot_ids =
                    reinterpret_cast<SlotId*>(aux_cursor);
                aux_cursor += n_in * sizeof(SlotId);
                te.output_slot_ids =
                    reinterpret_cast<SlotId*>(aux_cursor);
                aux_cursor += n_out * sizeof(SlotId);
                if (n_scalars > 0)
                    std::memcpy(te.scalar_args, re.scalar_values,
                                n_scalars * sizeof(int64_t));
            }
        }

        const uint64_t t1 = bench::rdtsc_end();
        bench::do_not_optimize(ops);
        ns_p2a += static_cast<double>(t1 - t0) * nspc;
    }

    // ── P2b: Content hash only ────────────────────────────────────
    double ns_p2b = 0;
    {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();
        auto*       ref_graph = bg.build_trace(A, count);
        TraceEntry* ref_ops   = ref_graph->ops;

        for (uint32_t iter = 0; iter < iters; iter++) {
            const uint64_t t0 = bench::rdtsc_start();

            uint64_t content_h = 0x9E3779B97F4A7C15ULL;
            for (uint32_t i = 0; i < count; i++) {
                const auto& te = ref_ops[i];
                content_h = detail::wymix(content_h,
                                          te.schema_hash.raw());
                for (uint16_t j = 0; j < te.num_inputs; j++) {
                    const TensorMeta& m = te.input_metas[j];
                    uint64_t dim_h = 0;
                    for (uint8_t d = 0; d < m.ndim; d++) {
                        dim_h ^= static_cast<uint64_t>(m.sizes[d])
                               * detail::kDimMix[d];
                        dim_h ^= static_cast<uint64_t>(m.strides[d])
                               * detail::kDimMix[d + 8];
                    }
                    const uint64_t meta_packed =
                          static_cast<uint64_t>(std::to_underlying(m.dtype))
                        | (static_cast<uint64_t>(
                             std::to_underlying(m.device_type)) << 8)
                        | (static_cast<uint64_t>(
                             static_cast<uint8_t>(m.device_idx)) << 16);
                    content_h = detail::wymix(content_h ^ dim_h,
                                              meta_packed);
                }
                if (te.num_scalar_args > 0) {
                    const uint16_t n =
                        std::min(te.num_scalar_args, uint16_t{5});
                    for (uint16_t s = 0; s < n; s++) {
                        content_h ^= static_cast<uint64_t>(
                            te.scalar_args[s]);
                        content_h *= 0x100000001b3ULL;
                    }
                }
            }

            const uint64_t t1 = bench::rdtsc_end();
            bench::do_not_optimize(content_h);
            ns_p2b += static_cast<double>(t1 - t0) * nspc;
        }
    }

    // ── P2c: PtrMap lookup ────────────────────────────────────────
    double ns_p2c = 0;
    {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();
        auto*       ref_graph = bg.build_trace(A, count);
        TraceEntry* ref_ops   = ref_graph->ops;

        for (uint32_t iter = 0; iter < iters; iter++) {
            bg.map_gen_++;
            if (bg.map_gen_ == 0) {
                std::memset(bg.scratch_map_, 0,
                            bg.map_cap_.get()
                                * sizeof(BackgroundThread::PtrSlot));
                bg.map_gen_ = 1;
            }
            auto*          local_map  = bg.scratch_map_;
            const uint8_t  local_gen  = bg.map_gen_;
            const uint32_t local_mask = bg.ptr_mask_;

            for (uint32_t i = 0; i < count; i++) {
                const auto& te = ref_ops[i];
                for (uint16_t j = 0; j < te.num_outputs; j++) {
                    void* ptr = te.output_metas[j].data_ptr;
                    if (ptr)
                        (void)BackgroundThread::ptr_map_insert(
                            local_map, local_gen, local_mask, ptr,
                            OpIndex{i},
                            static_cast<uint8_t>(j), SlotId{0});
                }
            }

            const uint64_t t0 = bench::rdtsc_start();

            uint32_t dummy_edges = 0;
            for (uint32_t i = 0; i < count; i++) {
                const auto& te = ref_ops[i];
                for (uint16_t j = 0; j < te.num_inputs; j++) {
                    void* ptr = te.input_metas[j].data_ptr;
                    auto  lookup = BackgroundThread::ptr_map_lookup(
                        local_map, local_gen, local_mask, ptr);
                    if (lookup.op_index.is_valid()) dummy_edges++;
                }
            }

            const uint64_t t1 = bench::rdtsc_end();
            bench::do_not_optimize(dummy_edges);
            ns_p2c += static_cast<double>(t1 - t0) * nspc;
        }
    }

    // ── P2d: PtrMap insert ────────────────────────────────────────
    double ns_p2d = 0;
    {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();
        auto*       ref_graph = bg.build_trace(A, count);
        TraceEntry* ref_ops   = ref_graph->ops;

        for (uint32_t iter = 0; iter < iters; iter++) {
            bg.map_gen_++;
            if (bg.map_gen_ == 0) {
                std::memset(bg.scratch_map_, 0,
                            bg.map_cap_.get()
                                * sizeof(BackgroundThread::PtrSlot));
                bg.map_gen_ = 1;
            }
            auto*          local_map  = bg.scratch_map_;
            const uint8_t  local_gen  = bg.map_gen_;
            const uint32_t local_mask = bg.ptr_mask_;

            const uint64_t t0 = bench::rdtsc_start();

            uint32_t aliases = 0;
            for (uint32_t i = 0; i < count; i++) {
                const auto& te = ref_ops[i];
                for (uint16_t j = 0; j < te.num_outputs; j++) {
                    void* ptr = te.output_metas[j].data_ptr;
                    if (!ptr) continue;
                    auto result = BackgroundThread::ptr_map_insert(
                        local_map, local_gen, local_mask, ptr,
                        OpIndex{i}, static_cast<uint8_t>(j), SlotId{0});
                    if (result.was_alias) aliases++;
                }
            }

            const uint64_t t1 = bench::rdtsc_end();
            bench::do_not_optimize(aliases);
            ns_p2d += static_cast<double>(t1 - t0) * nspc;
        }
    }

    // ── P3: Slot copy ─────────────────────────────────────────────
    double ns_p3 = 0;
    {
        bg.current_trace.assign(trace.entries.begin(),
                                trace.entries.end());
        bg.current_meta_starts.assign(trace.meta_starts.begin(),
                                      trace.meta_starts.end());
        bg.current_scope_hashes.assign(trace.scope_hashes.begin(),
                                       trace.scope_hashes.end());
        bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(),
                                          trace.callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{arena_bytes};
        repopulate();
        auto*          ref_graph = bg.build_trace(A, count);
        const uint32_t num_slots = ref_graph->num_slots;

        std::vector<BackgroundThread::SlotInfo> saved_slots(
            bg.scratch_slots_, bg.scratch_slots_ + num_slots);

        for (uint32_t iter = 0; iter < iters; iter++) {
            std::memcpy(bg.scratch_slots_, saved_slots.data(),
                        num_slots * sizeof(BackgroundThread::SlotInfo));
            bg.arena.~Arena();
            new (&bg.arena) Arena{arena_bytes};

            const uint64_t t0 = bench::rdtsc_start();

            auto* slots = bg.arena.alloc_array<TensorSlot>(A, num_slots);
            for (uint32_t s = 0; s < num_slots; s++) {
                slots[s].offset_bytes = 0;
                std::memcpy(&slots[s].nbytes, &bg.scratch_slots_[s],
                            sizeof(BackgroundThread::SlotInfo));
                slots[s].slot_id = SlotId{s};
                std::memset(slots[s].pad2, 0, sizeof(slots[s].pad2));
            }

            const uint64_t t1 = bench::rdtsc_end();
            bench::do_not_optimize(slots);
            ns_p3 += static_cast<double>(t1 - t0) * nspc;
        }
    }

    PhaseTiming phases[] = {
        {"P0  pre-scan",       ns_p0,  iters},
        {"P1  alloc+memset",   ns_p1,  iters},
        {"P2a copy fields",    ns_p2a, iters},
        {"P2b content hash",   ns_p2b, iters},
        {"P2c PtrMap lookup",  ns_p2c, iters},
        {"P2d PtrMap insert",  ns_p2d, iters},
        {"P3  slot copy",      ns_p3,  iters},
    };

    double sum = 0;
    for (auto& p : phases) sum += p.total_ns;
    print_phase_table(phases, 7, sum, iters);
}

// Default trace baked at configure time — see bench/CMakeLists.txt.
// SD1.5 U-Net (36499 ops) is the minimum meaningful production-scale
// signal for the build_trace pipeline. resnet18 (1386 ops) under-samples
// every per-op phase; vit_b (~15k) is borderline.
#ifdef CRUCIBLE_BENCH_DEFAULT_TRACE
constexpr const char* kDefaultTrace = CRUCIBLE_BENCH_DEFAULT_TRACE;
#else
constexpr const char* kDefaultTrace = nullptr;
#endif

// Default iteration count per phase. Tuned against SD1.5 on i7-14700HX
// to land inside a ~10 s total wall budget:
//   bench::run Report  (500 samples × 3.3 ms)                 ≈ 1.7 s
//   Top-level measure  (1200 iters × 2.0 ms)                  ≈ 2.4 s
//   build_csr + plan   (1200 × 460 µs)                        ≈ 0.5 s
//   Phase 2 sub-parts  (7 × 1200 × [measured + ~1.5 ms setup]) ≈ 4.5 s
// Total ≈ 9 s. Smaller traces (resnet18, vit_b) finish in a fraction.
// Override explicitly: `./bench_phases <trace> <iters>`.
constexpr uint32_t kDefaultIters = 1200;

} // namespace

int main(int argc, char* argv[]) {
    const char* trace_path = (argc >= 2) ? argv[1] : kDefaultTrace;
    if (!trace_path) {
        std::fprintf(stderr,
            "usage: %s <file.crtrace> [iters]\n"
            "error: no default trace baked in at configure time\n",
            argv[0]);
        return 1;
    }

    bench::print_system_info();
    bench::elevate_priority();
    const bool json = bench::env_json();

    std::printf("TSC ratio: %.4f ns/cycle\n", bench::Timer::ns_per_cycle());

    auto trace = load_trace(trace_path);
    if (!trace) {
        std::fprintf(stderr, "error: could not load %s\n", trace_path);
        std::fprintf(stderr,
            "usage: %s [file.crtrace] [iters]\n", argv[0]);
        return 1;
    }
    std::printf("Loaded %s: %u ops, %u metas\n",
                trace_path, trace->num_ops, trace->num_metas);

    const uint32_t iters = (argc >= 3)
        ? static_cast<uint32_t>(std::atoi(argv[2])) : kDefaultIters;

    MetaLog meta_log;
    meta_log.reset();

    BackgroundThread bg;
    bg.meta_log.set(&meta_log);

    std::printf("\n=== full pipeline (bench::run with BPF sensing) ===\n");
    std::vector<bench::Report> reports;
    reports.push_back(run_fullpipeline(bg, meta_log, *trace));
    bench::emit_reports_text(reports);

    bench_phases_toplevel(bg, meta_log, *trace, iters);
    bench_phase2_subparts(bg, meta_log, *trace, iters);

    bench::emit_reports_json(reports, json);

    std::printf("\nDone.\n");
    return 0;
}
