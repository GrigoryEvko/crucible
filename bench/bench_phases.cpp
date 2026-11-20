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
//   P1b Meta copy      the run of the MetaLog into a new arena
//   P2a Copy fields    schema/shape/scope/callsite + meta ptrs + aux ptrs
//   P2b Content hash   fmix64 chain over all tensor dims for streaming hash
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
#include "bench_trace.h"

#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceLoader.h>
#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <fixy/os/Args.h>
#include <foundation/Lifetime.h>
#include <foundation/reflect/Mix.h>

namespace crucible {

// The door of this bench to the scratch of the build stage, which
// BackgroundThread names as a friend.  The bench times each phase of a build
// against that scratch, on a BackgroundThread that runs no pipeline.
struct BuildScratchOfBench {
    [[nodiscard]] static ::fixy::MaskedArray<BackgroundThread::PtrSlot>& map(BackgroundThread& bg) noexcept {
        return bg.scratch_map_;
    }
    [[nodiscard]] static ::fixy::HeapArray<BackgroundThread::SlotInfo>& slots(BackgroundThread& bg) noexcept {
        return bg.scratch_slots_;
    }
    [[nodiscard]] static uint8_t& map_generation(BackgroundThread& bg) noexcept { return bg.map_gen_; }
    [[nodiscard]] static uint32_t slot_capacity(BackgroundThread const& bg) noexcept { return bg.slot_cap_max_.get(); }
    // The growth of the scratch buffers that the build makes after its scan.
    // The counts pass through as the build gives them.
    template <class... Counts>
    static void ensure_buffers(BackgroundThread& bg, ::foundation::effects::Alloc alloc, Counts... counts) {
        bg.ensure_scratch_buffers(alloc, counts...);
    }
};

}  // namespace crucible

using namespace crucible;

namespace {

// Benches run outside the normal bg thread, so the bench takes one
// background context from the test door and threads its .alloc token
// through every capability-tagged call.
const auto BG_CTX = ::foundation::effects::testing::bg();
constexpr auto A = BG_CTX.alloc;

struct PhaseTiming {
    ::fixy::TextView name{};
    double total_ns = 0.0;  // sum across all iterations
    uint64_t count = 0;  // number of iterations
};

// The time of a whole table, in nanoseconds, and the count of the iterations
// of each phase.
using TotalNs = ::fixy::Quantity<::fixy::unit::Nanosecond, double>;
using IterationCount = ::fixy::Quantity<::fixy::unit::Element, uint64_t>;

// The columns of a row of the phase table: the name, the time of one
// iteration, the time of all iterations in milliseconds, and the percent of
// the total.
using PhaseName = ::fixy::Padded<22, ::fixy::Align::Left>;
using PhaseNs = ::fixy::Fixed<1, 8>;
using PhaseMs = ::fixy::Fixed<2, 8>;
using PhasePercent = ::fixy::Fixed<1, 5>;
using PhaseHeading = ::fixy::Padded<8>;

void print_phase_table(::fixy::View<const PhaseTiming> phases, TotalNs total, IterationCount iterations) {
    using ::fixy::TextView;
    const double total_ns = total.raw();
    const auto iters = static_cast<double>(iterations.raw());
    bench::print("\n  {}  {}  {}  {}\n", PhaseName{TextView{"Phase"}}, PhaseHeading{TextView{"ns/op"}},
                 PhaseHeading{TextView{"total ms"}}, ::fixy::Padded<5>{TextView{"  %"}});
    bench::print("  {}  {}  {}  {}\n", PhaseName{TextView{"──────────────────────"}},
                 PhaseHeading{TextView{"────────"}}, PhaseHeading{TextView{"────────"}},
                 ::fixy::Padded<5>{TextView{"─────"}});

    for (const PhaseTiming& phase : phases) {
        const double ns_per = phase.total_ns / iters;
        const double pct = 100.0 * phase.total_ns / total_ns;
        bench::print("  {}  {}  {}  {}%\n", PhaseName{phase.name}, PhaseNs{ns_per}, PhaseMs{phase.total_ns / 1e6},
                     PhasePercent{pct});
    }

    double sum_ns = 0;
    for (const PhaseTiming& phase : phases)
        sum_ns += phase.total_ns;
    const double ns_per = total_ns / iters;
    const double sum_per = sum_ns / iters;

    bench::print("  {}  {}  {}  {}\n", PhaseName{TextView{"──────────────────────"}},
                 PhaseHeading{TextView{"────────"}}, PhaseHeading{TextView{"────────"}},
                 ::fixy::Padded<5>{TextView{"─────"}});
    bench::print("  {}  {}  {}  {}%\n", PhaseName{TextView{"SUM(phases)"}}, PhaseNs{sum_per}, PhaseMs{sum_ns / 1e6},
                 PhasePercent{100.0 * sum_ns / total_ns});
    bench::print("  {}  {}  {}  100.0%\n", PhaseName{TextView{"TOTAL (measured)"}}, PhaseNs{ns_per},
                 PhaseMs{total_ns / 1e6});
}

// build_trace gives an op with no metadata no arrays and zero tensor counts.
// The copy of its loop in the P2a phase does the same.
void check_ops_without_metas(::fixy::View<const TraceEntry> ops, ::fixy::View<const MetaIndex> meta_data) {
    for (const auto [op, meta_start] : ops.walk().try_zip(meta_data.walk()).expect("each op has a metadata start")) {
        if (meta_start->is_valid()) continue;
        const TraceEntry& op_record = *op;
        CRUCIBLE_BENCH_CHECK(op_record.input_metas().size() == 0 && op_record.output_metas().size() == 0);
        CRUCIBLE_BENCH_CHECK(op_record.scalar_args().size() == 0 && op_record.input_trace_indices().size() == 0);
        CRUCIBLE_BENCH_CHECK(op_record.input_slot_ids().size() == 0 && op_record.output_slot_ids().size() == 0);
        CRUCIBLE_BENCH_CHECK(op_record.num_inputs() == 0 && op_record.num_outputs() == 0);
    }
}

// Builds the trace graph of the recorded trace into scratch.  The build reads
// the arrays of the trace and does not write them.  No pipeline of the bench
// runs, so each build takes a fresh proof of quiescence.
BackgroundThread::TraceBuild build_recorded(BackgroundThread& bg, ::fixy::BumpArena& scratch,
                                            const LoadedTrace& trace) {
    return bg.build_trace_from(bg.prove_quiescence(), A, scratch, trace.entries.view(), trace.meta_starts.view(),
                               trace.scope_hashes.view(), trace.callsite_hashes.view(), trace.depths.view(),
                               bg.numerics());
}

// ── Top-level Report via bench::run ──────────────────────────────────
//
// The full build_trace pipeline on a real recorded trace. The body
// opens an epoch of the scratch arena and refills the MetaLog every call,
// then runs the build, then ends the epoch. The build does not write the
// arrays of the trace, so each sample measures the same work.
bench::Report run_fullpipeline(BackgroundThread& bg, MetaLog& meta_log, const LoadedTrace& trace) {
    const size_t arena_bytes = ::fixy::max(size_t{1} << 20, static_cast<size_t>(trace.num_ops) * 256);
    ::fixy::BumpArena scratch{arena_bytes};

    bench::Run r{bench::format_name("build_trace (full pipeline, {} ops)", trace.num_ops)};
    if (const int c = bench::env_core(); c >= 0) (void)r.core(c);
    return r.samples(500).warmup(10).batch(1).measure([&] {
        scratch.with_epoch([&]() noexcept {
            bench::refill_meta_log(meta_log, trace);
            auto built = build_recorded(bg, scratch, trace);
            bench::do_not_optimize(built);
        });
    });
}

// ── Phase tables via manual rdtsc bracketing ─────────────────────────
//
// This section does NOT use bench::run — the phase-level timings are
// the whole point, and the standard harness measures entire bodies.
// Each phase's rdtsc_start/end pair reads the TSC with LFENCE bracketing
// (Intel "How to Benchmark..." 2010 §3.2.1, via bench::rdtsc_start/end).

void bench_phases_toplevel(BackgroundThread& bg, MetaLog& meta_log, const LoadedTrace& trace, uint32_t iters) {
    bench::print("\n=== Top-level phase breakdown ({} ops, {} iters) ===\n", trace.num_ops, iters);

    const size_t arena_bytes = ::fixy::max(size_t{1} << 20, static_cast<size_t>(trace.num_ops) * 256);
    const double nspc = bench::Timer::ns_per_cycle();
    ::fixy::BumpArena scratch{arena_bytes};
    // The planner writes the offsets into the slots, so each run plans a copy
    // of them here.
    ::fixy::BumpArena slot_scratch{arena_bytes};

    // Warmup: 5 full pipeline runs to hit the cache.
    for (uint32_t w = 0; w < 5; w++) {
        scratch.with_epoch([&]() noexcept {
            bench::refill_meta_log(meta_log, trace);
            auto built = build_recorded(bg, scratch, trace);
            bench::do_not_optimize(built);
        });
    }

    // Measurement: run build_trace N times, accumulate.
    double total_ns = 0;
    ::fixy::HeapArray<double> samples = ::fixy::mint_heap_array(A, iters, 0.0);

    for (uint32_t iter = 0; iter < iters; iter++) {
        const double ns = scratch.with_epoch([&]() noexcept {
            bench::refill_meta_log(meta_log, trace);

            const uint64_t t0 = bench::rdtsc_start();
            auto built = build_recorded(bg, scratch, trace);
            const uint64_t t1 = bench::rdtsc_end();

            bench::do_not_optimize(built);
            return static_cast<double>(t1 - t0) * nspc;
        });
        total_ns += ns;
        *samples.get(iter).expect("the samples hold one time for each iteration") = ns;
    }

    if (iters == 0) ::fixy::fatal("bench_phases: the top-level breakdown takes at least one iteration");
    const ::fixy::HeapArray<double> sorted = bench::sorted_samples(std::move(samples));
    bench::print("  full pipeline: min={}  med={}  max={} ns/op\n",
                 ::fixy::Fixed<1>{*sorted.get(0).expect("the first sample of a run of at least one")},
                 ::fixy::Fixed<1>{*sorted.get(sorted.size() / 2).expect("the middle sample of the run")},
                 ::fixy::Fixed<1>{*sorted.get(sorted.size() - 1).expect("the last sample of the run")});

    // Isolated build_csr + compute_memory_plan.
    double ns_csr = 0;
    double ns_memplan = 0;
    {
        // One full build to get realistic edge data.  The graph of the
        // reference build stays in ref_scratch until the end of this block,
        // so the copies need no epoch.
        ::fixy::BumpArena ref_scratch{arena_bytes};
        bench::refill_meta_log(meta_log, trace);
        auto [ref, ref_region] = build_recorded(bg, ref_scratch, trace).expect("the recorded trace builds");

        const uint32_t num_ops = ref->num_ops.get();
        const uint32_t num_slots = ref->num_slots.get();

        // The forward edges are in the order of their sources.
        const ::fixy::HeapArray<Edge> saved_edges = ::fixy::mint_heap_array_copy<Edge>(A, ref->fwd_edges);

        const ::fixy::View<const TensorSlot> saved_slots = ref_region.slots();

        // build_csr
        for (uint32_t iter = 0; iter < iters; iter++) {
            ::fixy::BumpArena csr_arena{1 << 18};
            const ::fixy::Ref<TraceGraph> graph2 = alloc_trace_graph(A, csr_arena);

            const uint64_t t0 = bench::rdtsc_start();
            build_csr(A, csr_arena, *graph2, saved_edges.view(), num_ops);
            const uint64_t t1 = bench::rdtsc_end();

            bench::do_not_optimize(graph2);
            ns_csr += static_cast<double>(t1 - t0) * nspc;
        }

        // compute_memory_plan
        for (uint32_t iter = 0; iter < iters; iter++) {
            const double ns = slot_scratch.with_epoch([&]() noexcept {
                const ::fixy::View<TensorSlot> slots = slot_scratch.array<TensorSlot>(A, num_slots, TensorSlot{});
                ::fixy::copy(slots, saved_slots).expect("the copy holds one entry for each slot");

                MemoryPlan plan_storage{};
                return scratch.with_epoch([&]() noexcept {
                    const uint64_t t0 = bench::rdtsc_start();
                    const ::fixy::Ref<MemoryPlan> plan = bg.compute_memory_plan(A, plan_storage, scratch, slots)
                                                             .expect("the slots of the bench fit in 64 bits");
                    const uint64_t t1 = bench::rdtsc_end();

                    bench::do_not_optimize(plan);
                    return static_cast<double>(t1 - t0) * nspc;
                });
            });
            ns_memplan += ns;
        }

        const double ns_other = total_ns - ns_csr - ns_memplan;

        const ::fixy::FixedArray<PhaseTiming, 3> phases{
            PhaseTiming{"Main loop (P0-P3)", ns_other, iters},
            PhaseTiming{"build_csr (P4)", ns_csr, iters},
            PhaseTiming{"memory_plan (P5)", ns_memplan, iters},
        };
        print_phase_table(phases.view(), TotalNs{total_ns}, IterationCount{iters});
    }
}

void bench_phase2_subparts(BackgroundThread& bg, MetaLog& meta_log, const LoadedTrace& trace, uint32_t iters) {
    bench::print("\n=== Phase 2 sub-part breakdown ({} ops, {} iters) ===\n", trace.num_ops, iters);

    const size_t arena_bytes = ::fixy::max(size_t{1} << 20, static_cast<size_t>(trace.num_ops) * 256);
    const uint32_t count = trace.num_ops;
    const double nspc = bench::Timer::ns_per_cycle();
    // The phases that copy the loop of build_trace take their buffers from
    // the heap, as build_trace does.  build_trace takes its graph from
    // build_scratch.
    ::fixy::BumpArena build_scratch{arena_bytes};

    uint32_t trace_total_inputs = 0, trace_total_outputs = 0;
    for (const TraceRing::Entry& entry : trace.entries) {
        trace_total_inputs += entry.num_inputs;
        trace_total_outputs += entry.num_outputs;
    }
    (void)trace_total_inputs;
    (void)trace_total_outputs;

    // One build to prime scratch buffers and reference ops.
    build_scratch.with_epoch([&]() noexcept {
        bench::refill_meta_log(meta_log, trace);
        auto ref = build_recorded(bg, build_scratch, trace);
        bench::do_not_optimize(ref);
    });

    // ── P0: Pre-scan ──────────────────────────────────────────────
    double ns_p0 = 0;
    for (uint32_t iter = 0; iter < iters; iter++) {
        const uint64_t t0 = bench::rdtsc_start();

        uint64_t max_meta_end = 0;
        MetaIndex first_meta = MetaIndex::none();
        uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
        // The reads of the scan of build_trace_from: one branded index of an
        // op reads each array.
        const uint32_t scanned = ::fixy::mint_branded_scope(trace.entries.view(), [&](auto trace_data) noexcept {
            const auto meta_data =
                trace_data.parallel(trace.meta_starts.view()).expect("the arrays of a trace are parallel");
            uint32_t ops = 0;
            for (const auto index : trace_data.indices()) {
                const MetaIndex ms = meta_data.at(index);
                const TraceRing::Entry& re = trace_data.at(index);
                if (ms.is_valid()) {
                    if (!first_meta.is_valid()) first_meta = ms;
                    const uint64_t end = ms.raw() + re.num_inputs + re.num_outputs;
                    if (end > max_meta_end) max_meta_end = end;
                }
                total_inputs += re.num_inputs;
                total_outputs += re.num_outputs;
                total_scalars += ::fixy::min(uint32_t{re.num_scalar_args}, kScalarSlots);
                ++ops;
            }
            return ops;
        });

        const uint64_t t1 = bench::rdtsc_end();

        bench::do_not_optimize(scanned);

        bench::do_not_optimize(max_meta_end);
        bench::do_not_optimize(total_inputs);
        bench::do_not_optimize(total_outputs);
        bench::do_not_optimize(total_scalars);
        ns_p0 += static_cast<double>(t1 - t0) * nspc;
    }

    // ── P1: Alloc + PtrMap gen + memset ───────────────────────────
    double ns_p1 = 0;
    for (uint32_t iter = 0; iter < iters; iter++) {
        uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
        for (const TraceRing::Entry& entry : trace.entries) {
            total_inputs += entry.num_inputs;
            total_outputs += entry.num_outputs;
            total_scalars += ::fixy::min(uint32_t{entry.num_scalar_args}, kScalarSlots);
        }

        const uint64_t t0 = bench::rdtsc_start();

        ::fixy::HeapArray<TraceEntry> op_buffer = ::fixy::mint_heap_array(A, count, TraceEntry{});
        ::fixy::HeapArray<int64_t> scalar_buffer = ::fixy::mint_heap_array(A, total_scalars, int64_t{0});
        ::fixy::HeapArray<OpIndex> producer_buffer = ::fixy::mint_heap_array(A, total_inputs, OpIndex{});
        ::fixy::HeapArray<SlotId> slot_buffer = ::fixy::mint_heap_array(A, total_inputs + total_outputs, SlotId{});
        BuildScratchOfBench::ensure_buffers(bg, A, total_inputs, total_outputs);
        const uint32_t slot_cap = ::fixy::min(BuildScratchOfBench::slot_capacity(bg),
                                              ::fixy::max(uint32_t{256}, total_inputs + total_outputs));
        for (BackgroundThread::SlotInfo& slot_info : BuildScratchOfBench::slots(bg)
                                                         .view()
                                                         .window(0, slot_cap)
                                                         .expect("the slot scratch holds slot_cap slots")) {
            slot_info = BackgroundThread::SlotInfo{};
        }

        const uint64_t t1 = bench::rdtsc_end();

        bench::do_not_optimize(op_buffer);
        bench::do_not_optimize(scalar_buffer);
        bench::do_not_optimize(producer_buffer);
        bench::do_not_optimize(slot_buffer);
        ns_p1 += static_cast<double>(t1 - t0) * nspc;
    }

    // ── P1b: Copy the run of the metadata log into the arena ──────
    // The copy goes into a new arena in each iteration.  A running pipeline
    // also gives each region a memory of its own.  The time holds the first
    // touch of each page of the copy.
    double ns_p1b = 0;
    {
        bench::refill_meta_log(meta_log, trace);
        uint64_t max_meta_end = 0;
        MetaIndex first_meta = MetaIndex::none();
        for (const auto [entry, meta_start] : trace.entries.view()
                                                  .walk()
                                                  .try_zip(trace.meta_starts.view().walk())
                                                  .expect("the arrays of a trace are parallel")) {
            const MetaIndex ms = *meta_start;
            if (ms.is_valid()) {
                if (!first_meta.is_valid()) first_meta = ms;
                const uint64_t end = ms.raw() + entry->num_inputs + entry->num_outputs;
                if (end > max_meta_end) max_meta_end = end;
            }
        }
        const uint32_t total_metas =
            first_meta.is_valid() ? static_cast<uint32_t>(max_meta_end - first_meta.raw()) : uint32_t{0};

        for (uint32_t iter = 0; iter < iters; iter++) {
            const uint64_t t0 = bench::rdtsc_start();
            ::fixy::HeapArray<TensorMeta> meta_buffer = ::fixy::mint_heap_array(A, total_metas, TensorMeta{});
            meta_log.copy_run(first_meta.raw(), meta_buffer.view());
            const uint64_t t1 = bench::rdtsc_end();

            bench::do_not_optimize(meta_buffer);
            ns_p1b += static_cast<double>(t1 - t0) * nspc;
        }
    }

    // ── P2a: Copy fields (no hash, no PtrMap) ─────────────────────
    double ns_p2a = 0;
    for (uint32_t iter = 0; iter < iters; iter++) {
        bench::refill_meta_log(meta_log, trace);

        uint64_t max_meta_end = 0;
        MetaIndex first_meta = MetaIndex::none();
        uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
        for (const auto [entry, meta_start] : trace.entries.view()
                                                  .walk()
                                                  .try_zip(trace.meta_starts.view().walk())
                                                  .expect("the arrays of a trace are parallel")) {
            const MetaIndex ms = *meta_start;
            const TraceRing::Entry& re = *entry;
            if (ms.is_valid()) {
                if (!first_meta.is_valid()) first_meta = ms;
                const uint64_t end = ms.raw() + re.num_inputs + re.num_outputs;
                if (end > max_meta_end) max_meta_end = end;
            }
            total_inputs += re.num_inputs;
            total_outputs += re.num_outputs;
            total_scalars += ::fixy::min(uint32_t{re.num_scalar_args}, kScalarSlots);
        }

        ::fixy::HeapArray<TraceEntry> op_buffer = ::fixy::mint_heap_array(A, count, TraceEntry{});
        const uint32_t total_metas =
            first_meta.is_valid() ? static_cast<uint32_t>(max_meta_end - first_meta.raw()) : uint32_t{0};
        ::fixy::HeapArray<TensorMeta> meta_buffer = ::fixy::mint_heap_array(A, total_metas, TensorMeta{});
        meta_log.copy_run(first_meta.raw(), meta_buffer.view());
        ::fixy::HeapArray<int64_t> scalar_buffer = ::fixy::mint_heap_array(A, total_scalars, int64_t{0});
        ::fixy::HeapArray<OpIndex> producer_buffer = ::fixy::mint_heap_array(A, total_inputs, OpIndex{});
        ::fixy::HeapArray<SlotId> slot_buffer = ::fixy::mint_heap_array(A, total_inputs + total_outputs, SlotId{});
        const ::fixy::View<TraceEntry> ops = op_buffer.view();
        const ::fixy::View<TensorMeta> meta_run = meta_buffer.view();
        const ::fixy::View<int64_t> scalar_run = scalar_buffer.view();
        const ::fixy::View<OpIndex> producer_run = producer_buffer.view();
        const ::fixy::View<SlotId> slot_run = slot_buffer.view();
        size_t scalar_cursor = 0;
        size_t producer_cursor = 0;
        size_t slot_cursor = 0;

        const uint64_t t0 = bench::rdtsc_start();

        // The reads of the loop of build_trace_from: one branded index of an
        // op reads each of the five arrays and the op buffer.
        const uint32_t copied = ::fixy::mint_branded_scope(trace.entries.view(), [&](auto trace_data) noexcept {
            const auto meta_data =
                trace_data.parallel(trace.meta_starts.view()).expect("the arrays of a trace are parallel");
            const auto scope_data =
                trace_data.parallel(trace.scope_hashes.view()).expect("the arrays of a trace are parallel");
            const auto callsite_data =
                trace_data.parallel(trace.callsite_hashes.view()).expect("the arrays of a trace are parallel");
            const auto depth_data =
                trace_data.parallel(trace.depths.view()).expect("the arrays of a trace are parallel");
            const auto op_data = trace_data.parallel(ops).expect("the op buffer holds one op for each entry");
            uint32_t copied_ops = 0;
            for (const auto index : trace_data.indices()) {
                const TraceRing::Entry& re = trace_data.at(index);
                const MetaIndex ms = meta_data.at(index);
                TraceEntry& te = op_data.at(index);
                ++copied_ops;

                te.schema_hash = re.schema_hash;
                te.shape_hash = re.shape_hash;
                te.scope_hash = scope_data.at(index);
                te.callsite_hash = callsite_data.at(index);
                te.depth = depth_data.at(index);
                te.scalar_kinds = re.scalar_kinds();
                te.grad_enabled = (re.op_flags & op_flag::GRAD_ENABLED) != 0;

                const uint8_t flags = re.op_flags;
                te.inference_mode = (flags & op_flag::INFERENCE_MODE) != 0;
                te.is_mutable = (flags & op_flag::IS_MUTABLE) != 0;
                te.training_phase = static_cast<TrainingPhase>((flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
                te.torch_function = (flags & op_flag::TORCH_FUNCTION) != 0;
                te.kernel_id = CKernelId::OPAQUE;
                const uint32_t n_scalars = ::fixy::min(uint32_t{re.num_scalar_args}, kScalarSlots);
                const ::fixy::View<int64_t> scalars =
                    scalar_run.window(scalar_cursor, n_scalars).expect("the scan counted the scalars");
                scalar_cursor += n_scalars;
                ::fixy::copy(scalars,
                             re.scalar_values.view().window(0, n_scalars).expect("an entry stores five scalars"))
                    .expect("the two runs hold the scalars of the op");
                te.set_scalar_args(scalars);

                // An op with no metadata index has no tensors.
                if (!ms.is_valid()) continue;
                const uint16_t n_in = re.num_inputs;
                const uint16_t n_out = re.num_outputs;
                const uint64_t meta_offset = ms.raw() - first_meta.raw();
                te.set_inputs(meta_run.window(meta_offset, n_in).expect("the copied run holds each op"),
                              producer_run.window(producer_cursor, n_in).expect("the scan counted the inputs"),
                              slot_run.window(slot_cursor, n_in).expect("the scan counted the inputs"));
                producer_cursor += n_in;
                slot_cursor += n_in;
                te.set_outputs(meta_run.window(meta_offset + n_in, n_out).expect("the copied run holds each op"),
                               slot_run.window(slot_cursor, n_out).expect("the scan counted the outputs"));
                slot_cursor += n_out;
            }
            return copied_ops;
        });

        const uint64_t t1 = bench::rdtsc_end();
        bench::do_not_optimize(op_buffer);
        bench::do_not_optimize(copied);
        ns_p2a += static_cast<double>(t1 - t0) * nspc;
        check_ops_without_metas(op_buffer.view(), trace.meta_starts.view());
    }

    // ── P2b: Content hash only ────────────────────────────────────
    double ns_p2b = 0;
    {
        // The region of the build owns the ops, so the ops outlive the epoch.
        // The graph dies at the end of the epoch, so only the region leaves it.
        BackgroundThread::BuiltRegion ref{};
        build_scratch.with_epoch([&]() noexcept {
            bench::refill_meta_log(meta_log, trace);
            ref = build_recorded(bg, build_scratch, trace).expect("the recorded trace builds").region;
        });
        const ::fixy::View<TraceEntry const> ref_ops = ref.ops();

        for (uint32_t iter = 0; iter < iters; iter++) {
            const uint64_t t0 = bench::rdtsc_start();

            uint64_t content_h = 0x9E3779B97F4A7C15ULL;
            for (const TraceEntry& te : ref_ops) {
                content_h = ::foundation::reflect::combine_ids(content_h, te.schema_hash.raw());
                for (const TensorMeta& m : te.input_metas()) {
                    const uint64_t dim_h = detail::dim_hash_scalar(m);
                    const uint64_t meta_packed = static_cast<uint64_t>(std::to_underlying(m.dtype))
                                               | (static_cast<uint64_t>(std::to_underlying(m.device_type)) << 8)
                                               | (static_cast<uint64_t>(static_cast<uint8_t>(m.device_idx)) << 16);
                    content_h = ::foundation::reflect::combine_ids(content_h, dim_h ^ meta_packed);
                }
                // The pipeline stores at most five scalars of an op.
                uint16_t folded = 0;
                for (const int64_t value : te.scalar_args()) {
                    if (folded == 5) break;
                    content_h ^= static_cast<uint64_t>(value);
                    content_h *= 0x100000001b3ULL;
                    ++folded;
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
        // The region of the build owns the ops, so the ops outlive the epoch.
        // The graph dies at the end of the epoch, so only the region leaves it.
        BackgroundThread::BuiltRegion ref{};
        build_scratch.with_epoch([&]() noexcept {
            bench::refill_meta_log(meta_log, trace);
            ref = build_recorded(bg, build_scratch, trace).expect("the recorded trace builds").region;
        });
        const ::fixy::View<TraceEntry const> ref_ops = ref.ops();

        for (uint32_t iter = 0; iter < iters; iter++) {
            BuildScratchOfBench::map_generation(bg)++;
            if (BuildScratchOfBench::map_generation(bg) == 0) {
                for (BackgroundThread::PtrSlot& slot : BuildScratchOfBench::map(bg).view()) {
                    slot = BackgroundThread::PtrSlot{};
                }
                BuildScratchOfBench::map_generation(bg) = 1;
            }
            const uint8_t local_gen = BuildScratchOfBench::map_generation(bg);

            uint32_t i = 0;
            for (const TraceEntry& te : ref_ops) {
                uint8_t port = 0;
                for (const TensorMeta& m : te.output_metas()) {
                    if (m.data_ptr.value() != nullptr)
                        (void)BackgroundThread::ptr_map_insert(BuildScratchOfBench::map(bg), local_gen,
                                                               crucible::storage_key(m), OpIndex{i}, port, SlotId{0});
                    ++port;
                }
                ++i;
            }

            const uint64_t t0 = bench::rdtsc_start();

            uint32_t dummy_edges = 0;
            for (const TraceEntry& te : ref_ops) {
                for (const TensorMeta& m : te.input_metas()) {
                    auto lookup = BackgroundThread::ptr_map_lookup(BuildScratchOfBench::map(bg), local_gen,
                                                                   crucible::storage_key(m));
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
        // The region of the build owns the ops, so the ops outlive the epoch.
        // The graph dies at the end of the epoch, so only the region leaves it.
        BackgroundThread::BuiltRegion ref{};
        build_scratch.with_epoch([&]() noexcept {
            bench::refill_meta_log(meta_log, trace);
            ref = build_recorded(bg, build_scratch, trace).expect("the recorded trace builds").region;
        });
        const ::fixy::View<TraceEntry const> ref_ops = ref.ops();

        for (uint32_t iter = 0; iter < iters; iter++) {
            BuildScratchOfBench::map_generation(bg)++;
            if (BuildScratchOfBench::map_generation(bg) == 0) {
                for (BackgroundThread::PtrSlot& slot : BuildScratchOfBench::map(bg).view()) {
                    slot = BackgroundThread::PtrSlot{};
                }
                BuildScratchOfBench::map_generation(bg) = 1;
            }
            const uint8_t local_gen = BuildScratchOfBench::map_generation(bg);

            const uint64_t t0 = bench::rdtsc_start();

            uint32_t aliases = 0;
            uint32_t i = 0;
            for (const TraceEntry& te : ref_ops) {
                uint8_t port = 0;
                for (const TensorMeta& m : te.output_metas()) {
                    const uint8_t this_port = port++;
                    if (m.data_ptr.value() == nullptr) continue;
                    auto result =
                        BackgroundThread::ptr_map_insert(BuildScratchOfBench::map(bg), local_gen,
                                                         crucible::storage_key(m), OpIndex{i}, this_port, SlotId{0});
                    if (result.was_alias) aliases++;
                }
                ++i;
            }

            const uint64_t t1 = bench::rdtsc_end();
            bench::do_not_optimize(aliases);
            ns_p2d += static_cast<double>(t1 - t0) * nspc;
        }
    }

    // ── P3: Slot copy ─────────────────────────────────────────────
    double ns_p3 = 0;
    {
        const uint32_t num_slots = build_scratch.with_epoch([&]() noexcept {
            bench::refill_meta_log(meta_log, trace);
            auto [ref_graph, ref_region] = build_recorded(bg, build_scratch, trace).expect("the recorded trace builds");
            return ref_graph->num_slots.get();
        });

        // The slots of the build, and a copy that each turn writes back.
        const ::fixy::View<BackgroundThread::SlotInfo> built_slots =
            BuildScratchOfBench::slots(bg)
                .view()
                .window(0, num_slots)
                .expect("the slot scratch holds the slots of the build");
        const ::fixy::HeapArray<BackgroundThread::SlotInfo> saved_slots =
            ::fixy::mint_heap_array_copy<BackgroundThread::SlotInfo>(A, built_slots);

        for (uint32_t iter = 0; iter < iters; iter++) {
            ::fixy::copy(built_slots, saved_slots.view()).expect("the two runs hold the slots of the build");

            const uint64_t t0 = bench::rdtsc_start();

            ::fixy::HeapArray<TensorSlot> slots =
                ::fixy::mint_heap_array_with<TensorSlot>(A, num_slots, [built_slots](size_t position) noexcept {
                    BackgroundThread::SlotInfo const& info =
                        *built_slots.get(position).expect("the slot of each position is built");
                    return TensorSlot{.offset_bytes = 0,
                                      .nbytes = info.nbytes,
                                      .birth_op = info.birth_op,
                                      .death_op = info.death_op,
                                      .dtype = info.dtype,
                                      .device_type = info.device_type,
                                      .device_idx = info.device_idx,
                                      .layout = info.layout,
                                      .is_external = info.is_external,
                                      .pad = {},
                                      .slot_id = SlotId{static_cast<uint32_t>(position)},
                                      .pad2 = {}};
                });

            const uint64_t t1 = bench::rdtsc_end();
            bench::do_not_optimize(slots);
            ns_p3 += static_cast<double>(t1 - t0) * nspc;
        }
    }

    const ::fixy::FixedArray<PhaseTiming, 8> phases{
        PhaseTiming{"P0  pre-scan", ns_p0, iters},       PhaseTiming{"P1  alloc+memset", ns_p1, iters},
        PhaseTiming{"P1b metadata copy", ns_p1b, iters}, PhaseTiming{"P2a copy fields", ns_p2a, iters},
        PhaseTiming{"P2b content hash", ns_p2b, iters},  PhaseTiming{"P2c PtrMap lookup", ns_p2c, iters},
        PhaseTiming{"P2d PtrMap insert", ns_p2d, iters}, PhaseTiming{"P3  slot copy", ns_p3, iters},
    };

    double sum = 0;
    for (const PhaseTiming& p : phases)
        sum += p.total_ns;
    print_phase_table(phases.view(), TotalNs{sum}, IterationCount{iters});
}

// The default trace comes from the configure step (bench/CMakeLists.txt): the
// recorded MiniGPT trace of the tree, 1091 ops.  A trace of more ops, such as
// the 36499 ops of an SD1.5 U-Net, gives the phases a cleaner split.
// An empty text means that the configure step baked in no trace.
#ifdef CRUCIBLE_BENCH_DEFAULT_TRACE
constexpr ::fixy::TextView kDefaultTrace{CRUCIBLE_BENCH_DEFAULT_TRACE};
#else
constexpr ::fixy::TextView kDefaultTrace{};
#endif

// Default iteration count per phase. Tuned against SD1.5 on i7-14700HX
// to land inside a ~10 s total wall budget:
//   bench::run Report  (500 samples × 3.3 ms)                 ≈ 1.7 s
//   Top-level measure  (1200 iters × 2.0 ms)                  ≈ 2.4 s
//   build_csr + plan   (1200 × 460 µs)                        ≈ 0.5 s
//   Phase 2 sub-parts  (8 × 1200 × [measured + ~1.5 ms setup]) ≈ 5 s
// Total ≈ 10 s. Smaller traces (resnet18, vit_b) finish in a fraction.
// Override explicitly: `./bench_phases <trace> <iters>`.
constexpr uint32_t kDefaultIters = 1200;

// The text of an argument of main, or fallback when main has no such
// argument.
[[nodiscard]] ::fixy::TextView text_or(::fixy::Option<::fixy::CText> const& argument,
                                       ::fixy::TextView fallback) noexcept {
    return argument.match([](::fixy::CText const& text) noexcept { return text.view(); },
                          [fallback]() noexcept { return fallback; });
}

// The bench with the arguments of main: the path of the trace file, and the
// count of the iterations of each phase.
int run_bench(::fixy::os::Args const& arguments) {
    const ::fixy::TextView program = text_or(arguments.get(0), ::fixy::TextView{"bench_phases"});
    const ::fixy::TextView trace_path = text_or(arguments.get(1), kDefaultTrace);
    if (trace_path.size() == 0) {
        ::fixy::report(::fixy::Sink::Err,
                       "usage: {} <file.crtrace> [iters]\n"
                       "error: no default trace baked in at configure time\n",
                       program);
        return 1;
    }

    bench::print_system_info();
    bench::elevate_priority();
    const bench::JsonTail json = bench::env_json();

    bench::print("TSC ratio: {} ns/cycle\n", ::fixy::Fixed<4>{bench::Timer::ns_per_cycle()});

    // A path that fails the path check counts as a file that does not open.
    auto sanitized_path = ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(trace_path));
    auto loaded = sanitized_path.is_ok()
                    ? load_trace(::fixy::InitLoadCtx{::foundation::effects::testing::init()},
                                 std::move(sanitized_path).expect("the path passed the check"))
                    : ::fixy::Result<LoadedTrace, TraceLoadError>{::fixy::err(TraceLoadError::OpenFailed)};
    if (loaded.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "error: could not load {}\n", trace_path);
        ::fixy::report(::fixy::Sink::Err, "usage: {} [file.crtrace] [iters]\n", program);
        return 1;
    }
    const LoadedTrace trace = std::move(loaded).expect("the load holds a trace after the check of its error");
    bench::print("Loaded {}: {} ops, {} metas\n", trace_path, trace.num_ops, trace.num_metas);

    // An iteration count that is no decimal number gives zero iterations, as
    // atoi gives.
    const uint32_t iters = arguments.get(2).match(
        [](::fixy::CText text) noexcept {
            return ::fixy::parse<uint32_t>(text.view())
                .match([](uint32_t value) noexcept { return value; },
                       [](::fixy::ParseError) noexcept { return uint32_t{0}; });
        },
        []() noexcept { return kDefaultIters; });

    MetaLog meta_log{::foundation::effects::testing::test().alloc};
    meta_log.reset();

    BackgroundThread bg{::foundation::effects::testing::test().alloc};
    bg.bind_meta_log(meta_log);

    bench::print("\n=== full pipeline (bench::run with BPF sensing) ===\n");
    ::fixy::FixedArray<bench::Report, 1> const reports{run_fullpipeline(bg, meta_log, trace)};
    bench::emit_reports_text(reports.view());

    bench_phases_toplevel(bg, meta_log, trace, iters);
    bench_phase2_subparts(bg, meta_log, trace, iters);

    bench::print("\nDone.\n");
    // The JSON tail comes last, because the reader reads each line after its
    // marker.
    bench::emit_reports_json(reports.view(), json);
    return 0;
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return run_bench(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
