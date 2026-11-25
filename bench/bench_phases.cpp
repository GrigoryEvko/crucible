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
//   P4  build_trace_graph  CSR property graph construction
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

// The door of this bench to the scratch of the build stage, which the state of
// the trace build names as a friend.  The bench times each phase of a build
// against that scratch, in a frame of with_trace_build_state, on a
// BackgroundThread that runs no pipeline.
struct BuildScratchOfBench {
    using State = BackgroundThread::TraceBuildState;

    [[nodiscard]] static ::fixy::MaskedArray<BackgroundThread::PtrSlot>& map(State& state) noexcept {
        return state.scratch_map_;
    }
    // Empties each slot of the map through the frame door of the map, as the
    // build does when its generation wraps.
    static void clear_map(State& state) noexcept {
        state.scratch_map_ = ::fixy::with_mut(std::move(state.scratch_map_), [](auto slots) noexcept {
            for (BackgroundThread::PtrSlot& slot : slots) {
                slot = BackgroundThread::PtrSlot{};
            }
        });
    }
    [[nodiscard]] static uint8_t& map_generation(State& state) noexcept { return state.map_gen_; }
    [[nodiscard]] static uint32_t slot_capacity(State const& state) noexcept { return state.slot_cap_max_.get(); }
    // The growth of the scratch buffers that the build makes after its scan.
    // The counts pass through as the build gives them.
    template <class... Counts>
    static void ensure_buffers(State& state, ::foundation::effects::Alloc alloc, Counts... counts) {
        state.ensure_scratch_buffers(alloc, counts...);
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

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

// The block size of a scratch arena of a trace: 256 bytes for each op, and
// at least 1 MiB.  The floor is a constant, and the bytes of the ops above
// the floor are the part of run time.
[[nodiscard]] ::fixy::ArenaBlockSize compute_trace_block(const LoadedTrace& trace) noexcept {
    constexpr size_t kFloorBytes = size_t{1} << 20;
    constexpr ::fixy::ArenaBlockSize kFloor{Bytes{kFloorBytes}};
    const size_t op_bytes = static_cast<size_t>(trace.num_ops) * 256;
    return kFloor.plus(Bytes{op_bytes - ::fixy::min(op_bytes, kFloorBytes)});
}

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

// Builds the trace graph of the recorded trace into the epoch, with the state
// of the trace build of a BackgroundThread that runs no pipeline
// (with_trace_build_state).  The build reads the arrays of the trace and does
// not write them.  Each body of an epoch captures the state, the metadata log
// by a const reference and the trace by a const reference, and not the
// BackgroundThread, so it reaches no arena of the BackgroundThread.
BackgroundThread::TraceBuild build_recorded(BackgroundThread::TraceBuildState& state, MetaLog const& meta_log,
                                            const LoadedTrace& trace, NumericsHash numerics,
                                            TraceScratch::Epoch& scratch) {
    return BackgroundThread::build_trace_from(state, meta_log, A, scratch, trace.entries.view(),
                                              trace.meta_starts.view(), trace.scope_hashes.view(),
                                              trace.callsite_hashes.view(), trace.depths.view(), numerics);
}

// ── Top-level Report via bench::run ──────────────────────────────────
//
// The full build_trace pipeline on a real recorded trace. The body
// refills the MetaLog every call, then opens an epoch of the scratch arena,
// runs the build, and ends the epoch. The build does not write the arrays
// of the trace, so each sample measures the same work.
bench::Report run_fullpipeline(BackgroundThread::TraceBuildState& state, crucible::test::OpenLog& meta_log,
                               const LoadedTrace& trace, NumericsHash numerics) {
    TraceScratch scratch{compute_trace_block(trace)};
    MetaLog const& log = *meta_log.view();

    bench::Run r{bench::format_name("build_trace (full pipeline, {} ops)", trace.num_ops)};
    if (const int c = bench::env_core(); c >= 0) (void)r.core(c);
    return r.samples(500).warmup(10).batch(1).measure([&] {
        bench::refill_meta_log(meta_log, trace);
        scratch.arena.with_epoch([&state, &log, &trace, numerics](TraceScratch::Epoch& epoch) noexcept {
            auto built = build_recorded(state, log, trace, numerics, epoch);
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

void bench_phases_toplevel(BackgroundThread::TraceBuildState& state, BackgroundThread::PlanTarget target,
                           crucible::test::OpenLog& meta_log, const LoadedTrace& trace, NumericsHash numerics,
                           uint32_t iters) {
    bench::print("\n=== Top-level phase breakdown ({} ops, {} iters) ===\n", trace.num_ops, iters);

    const ::fixy::ArenaBlockSize arena_block = compute_trace_block(trace);
    const double nspc = bench::Timer::ns_per_cycle();
    TraceScratch scratch{arena_block};
    // The sweep of each plan takes its arrays from this scratch.
    PlanScratch plan_scratch{arena_block};
    MetaLog const& log = *meta_log.view();

    // Warmup: 5 full pipeline runs to hit the cache.
    for (uint32_t w = 0; w < 5; w++) {
        bench::refill_meta_log(meta_log, trace);
        scratch.arena.with_epoch([&state, &log, &trace, numerics](TraceScratch::Epoch& epoch) noexcept {
            auto built = build_recorded(state, log, trace, numerics, epoch);
            bench::do_not_optimize(built);
        });
    }

    // Measurement: run build_trace N times, accumulate.
    // The mint runs the iterations in the order of the samples, one sample
    // for each iteration.
    double total_ns = 0;
    ::fixy::HeapArray<double> samples = ::fixy::mint_heap_array_with<double>(
        A, iters, [&scratch, &meta_log, &trace, &state, &log, &total_ns, numerics, nspc](std::size_t) noexcept {
            bench::refill_meta_log(meta_log, trace);
            const double ns =
                scratch.arena.with_epoch([&state, &log, &trace, numerics, nspc](TraceScratch::Epoch& epoch) noexcept {
                    const uint64_t t0 = bench::rdtsc_start();
                    auto built = build_recorded(state, log, trace, numerics, epoch);
                    const uint64_t t1 = bench::rdtsc_end();

                    bench::do_not_optimize(built);
                    return static_cast<double>(t1 - t0) * nspc;
                });
            total_ns += ns;
            return ns;
        });

    if (iters == 0) ::fixy::fatal("bench_phases: the top-level breakdown takes at least one iteration");
    const ::fixy::HeapArray<double> sorted = bench::sorted_samples(std::move(samples));
    bench::print("  full pipeline: min={}  med={}  max={} ns/op\n",
                 ::fixy::Fixed<1>{*sorted.get(0).expect("the first sample of a run of at least one")},
                 ::fixy::Fixed<1>{*sorted.get(sorted.size() / 2).expect("the middle sample of the run")},
                 ::fixy::Fixed<1>{*sorted.get(sorted.size() - 1).expect("the last sample of the run")});

    // Isolated build_trace_graph + compute_memory_plan.
    double ns_csr = 0;
    double ns_memplan = 0;
    {
        // One full build to get realistic edge data.  The graph of the
        // reference build dies with its epoch, so the epoch copies the
        // counts and the edges, and the region leaves it.
        TraceScratch ref_scratch{arena_block};
        bench::refill_meta_log(meta_log, trace);
        uint32_t num_ops = 0;
        uint32_t num_slots = 0;
        ::fixy::HeapArray<Edge> saved_edges{};
        BackgroundThread::BuiltRegion ref_region{};
        ref_scratch.arena.with_epoch([&state, &log, &trace, numerics, &num_ops, &num_slots, &saved_edges,
                                      &ref_region](TraceScratch::Epoch& epoch) noexcept {
            auto [ref, region] = build_recorded(state, log, trace, numerics, epoch).expect("the recorded trace builds");
            num_ops = ref->num_ops;
            num_slots = ref->num_slots;
            // The forward edges are in the order of their sources.
            saved_edges = ::fixy::mint_heap_array_copy<Edge>(A, ref->fwd_edges.view());
            ref_region = std::move(region);
        });

        const ::fixy::View<const TensorSlot> saved_slots = ref_region.slots();

        // build_trace_graph, with fresh blocks in each iteration.  The time
        // holds the bump of the graph in the epoch and the counting sort.
        for (uint32_t iter = 0; iter < iters; iter++) {
            TraceScratch csr_scratch{Bytes{size_t{1} << 18}};
            ns_csr += csr_scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
                const uint64_t t0 = bench::rdtsc_start();
                const TraceGraphRef graph2 =
                    build_trace_graph(A, epoch, TraceGraphShape{.num_ops = num_ops}, saved_edges.view());
                const uint64_t t1 = bench::rdtsc_end();

                bench::do_not_optimize(graph2);
                return static_cast<double>(t1 - t0) * nspc;
            });
        }

        // compute_memory_plan.  The planner writes the offsets into the
        // slots, so each run plans a copy of them in an arena of its own.
        for (uint32_t iter = 0; iter < iters; iter++) {
            ::fixy::BumpArena<DagArena> slot_arena = ::fixy::mint_bump_arena<DagArena>(arena_block);
            auto slots_run = slot_arena.array<TensorSlot>(A, num_slots, TensorSlot{});
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<TensorSlot> slots = slots_run.view();
            ::fixy::copy(slots, saved_slots).expect("the copy holds one entry for each slot");

            // The plan is in the arena of the slots.
            const DagRef<MemoryPlan> plan_storage = slot_arena.make<MemoryPlan>(A);
            ns_memplan += plan_scratch.arena.with_epoch(
                [target, plan_storage, &slots_run, nspc](PlanScratch::Epoch& epoch) noexcept {
                    const uint64_t t0 = bench::rdtsc_start();
                    const DagRef<MemoryPlan> plan =
                        BackgroundThread::compute_memory_plan(A, target, plan_storage, epoch, slots_run)
                            .expect("the slots of the bench fit in 64 bits");
                    const uint64_t t1 = bench::rdtsc_end();

                    bench::do_not_optimize(plan);
                    return static_cast<double>(t1 - t0) * nspc;
                });
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

void bench_phase2_subparts(BackgroundThread::TraceBuildState& state, crucible::test::OpenLog& meta_log,
                           const LoadedTrace& trace, NumericsHash numerics, uint32_t iters) {
    bench::print("\n=== Phase 2 sub-part breakdown ({} ops, {} iters) ===\n", trace.num_ops, iters);

    const uint32_t count = trace.num_ops;
    const double nspc = bench::Timer::ns_per_cycle();
    // The phases that copy the loop of build_trace take their buffers from
    // the heap, as build_trace does.  build_trace takes its graph from
    // build_scratch.
    TraceScratch build_scratch{compute_trace_block(trace)};
    MetaLog const& log = *meta_log.view();

    uint32_t trace_total_inputs = 0, trace_total_outputs = 0;
    for (const TraceRing::Entry& entry : trace.entries) {
        trace_total_inputs += entry.num_inputs;
        trace_total_outputs += entry.num_outputs;
    }
    (void)trace_total_inputs;
    (void)trace_total_outputs;

    // One build to prime scratch buffers and reference ops.
    bench::refill_meta_log(meta_log, trace);
    build_scratch.arena.with_epoch([&state, &log, &trace, numerics](TraceScratch::Epoch& epoch) noexcept {
        auto ref = build_recorded(state, log, trace, numerics, epoch);
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

        // The runs of the ops come from an arena of the region, and the slots
        // of the build from an epoch of the build arena, as build_trace takes
        // them.  The block holds the sum of the alignments, a constant, plus
        // the bytes of the runs.
        constexpr ::fixy::ArenaBlockSize kRunAlignments{
            Bytes{alignof(TraceEntry) + alignof(int64_t) + alignof(OpIndex) + alignof(SlotId)}};
        const size_t run_bytes = size_t{count} * sizeof(TraceEntry) + size_t{total_scalars} * sizeof(int64_t)
                               + size_t{total_inputs} * sizeof(OpIndex)
                               + (size_t{total_inputs} + total_outputs) * sizeof(SlotId);
        ::fixy::Box<::fixy::BumpArena<DagArena>> region_arena =
            ::fixy::mint_box<::fixy::BumpArena<DagArena>>(A, kRunAlignments.plus(Bytes{run_bytes}));
        const auto op_arena_run = region_arena->array(A, count, TraceEntry{});
        const auto scalar_arena_run = region_arena->array(A, total_scalars, int64_t{0});
        const auto producer_arena_run = region_arena->array(A, total_inputs, OpIndex{});
        const auto slot_arena_run = region_arena->array(A, total_inputs + total_outputs, SlotId{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry const> op_run = op_arena_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<int64_t const> scalar_run = scalar_arena_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<OpIndex const> producer_run = producer_arena_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<SlotId const> slot_run = slot_arena_run.view();
        BuildScratchOfBench::ensure_buffers(state, A, ::fixy::Quantity<::fixy::unit::Element, uint32_t>{total_outputs});
        const uint32_t slot_cap = ::fixy::min(BuildScratchOfBench::slot_capacity(state),
                                              ::fixy::max(uint32_t{256}, total_inputs + total_outputs));
        build_scratch.arena.with_epoch([slot_cap](TraceScratch::Epoch& epoch) noexcept {
            const auto slot_infos = epoch.array(A, slot_cap, BackgroundThread::SlotInfo{});
            bench::do_not_optimize(slot_infos);
        });

        const uint64_t t1 = bench::rdtsc_end();

        bench::do_not_optimize(op_run);
        bench::do_not_optimize(scalar_run);
        bench::do_not_optimize(producer_run);
        bench::do_not_optimize(slot_run);
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
        const crucible::test::OpenLog& meta_log_read = meta_log;

        for (uint32_t iter = 0; iter < iters; iter++) {
            // The buffer is new in each iteration, so its frame is in the
            // timed region.  The frame gives the buffer back with no
            // assignment.
            const uint64_t t0 = bench::rdtsc_start();
            ::fixy::HeapArray<TensorMeta> meta_buffer =
                ::fixy::with_mut(::fixy::mint_heap_array(A, total_metas, TensorMeta{}),
                                 [&meta_log = meta_log_read, first_meta](auto run) noexcept {
                                     meta_log.copy_run(first_meta.raw(), std::move(run));
                                 });
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

        // The runs of the ops come from an arena of the region, as
        // build_trace takes them.  The block holds the sum of the alignments,
        // a constant, plus the bytes of the runs.
        const uint32_t total_metas =
            first_meta.is_valid() ? static_cast<uint32_t>(max_meta_end - first_meta.raw()) : uint32_t{0};
        constexpr ::fixy::ArenaBlockSize kRunAlignments{
            Bytes{alignof(TraceEntry) + alignof(TensorMeta) + alignof(int64_t) + alignof(OpIndex) + alignof(SlotId)}};
        const size_t run_bytes = size_t{count} * sizeof(TraceEntry) + size_t{total_metas} * sizeof(TensorMeta)
                               + size_t{total_scalars} * sizeof(int64_t) + size_t{total_inputs} * sizeof(OpIndex)
                               + (size_t{total_inputs} + total_outputs) * sizeof(SlotId);
        ::fixy::Box<::fixy::BumpArena<DagArena>> region_arena =
            ::fixy::mint_box<::fixy::BumpArena<DagArena>>(A, kRunAlignments.plus(Bytes{run_bytes}));
        auto ops_arena_run = region_arena->array(A, count, TraceEntry{});
        auto meta_arena_run = region_arena->array(A, total_metas, TensorMeta{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_arena_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TensorMeta> meta_run = meta_arena_run.view();
        meta_log.copy_run(first_meta.raw(), meta_run);
        auto scalar_arena_run = region_arena->array(A, total_scalars, int64_t{0});
        const auto producer_arena_run = region_arena->array(A, total_inputs, OpIndex{});
        const auto slot_arena_run = region_arena->array(A, total_inputs + total_outputs, SlotId{});
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
                te.training_phase =
                    ::fixy::enum_from_field<TrainingPhase, 2>((flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
                te.torch_function = (flags & op_flag::TORCH_FUNCTION) != 0;
                te.kernel_id = CKernelId::OPAQUE;
                const uint32_t n_scalars = ::fixy::min(uint32_t{re.num_scalar_args}, kScalarSlots);
                auto scalars =
                    scalar_arena_run
                        .window(::fixy::RunStart{::fixy::ElementCount{scalar_cursor}}, ::fixy::ElementCount{n_scalars})
                        .expect("the scan counted the scalars");
                scalar_cursor += n_scalars;
                ::fixy::copy(scalars.view(),
                             re.scalar_values.view().window(0, n_scalars).expect("an entry stores five scalars"))
                    .expect("the two runs hold the scalars of the op");
                te.set_scalar_args(scalars);

                // An op with no metadata index has no tensors.
                if (!ms.is_valid()) continue;
                const uint16_t n_in = re.num_inputs;
                const uint16_t n_out = re.num_outputs;
                const uint64_t meta_offset = ms.raw() - first_meta.raw();
                te.set_inputs(
                    meta_arena_run
                        .window(::fixy::RunStart{::fixy::ElementCount{meta_offset}}, ::fixy::ElementCount{n_in})
                        .expect("the copied run holds each op"),
                    producer_arena_run
                        .window(::fixy::RunStart{::fixy::ElementCount{producer_cursor}}, ::fixy::ElementCount{n_in})
                        .expect("the scan counted the inputs"),
                    slot_arena_run
                        .window(::fixy::RunStart{::fixy::ElementCount{slot_cursor}}, ::fixy::ElementCount{n_in})
                        .expect("the scan counted the inputs"));
                producer_cursor += n_in;
                slot_cursor += n_in;
                te.set_outputs(
                    meta_arena_run
                        .window(::fixy::RunStart{::fixy::ElementCount{meta_offset + n_in}}, ::fixy::ElementCount{n_out})
                        .expect("the copied run holds each op"),
                    slot_arena_run
                        .window(::fixy::RunStart{::fixy::ElementCount{slot_cursor}}, ::fixy::ElementCount{n_out})
                        .expect("the scan counted the outputs"));
                slot_cursor += n_out;
            }
            return copied_ops;
        });

        const uint64_t t1 = bench::rdtsc_end();
        bench::do_not_optimize(ops);
        bench::do_not_optimize(copied);
        ns_p2a += static_cast<double>(t1 - t0) * nspc;
        check_ops_without_metas(ops, trace.meta_starts.view());
    }

    // ── P2b: Content hash only ────────────────────────────────────
    double ns_p2b = 0;
    {
        // The region of the build owns the ops, so the ops outlive the epoch.
        // The graph dies at the end of the epoch, so only the region leaves it.
        BackgroundThread::BuiltRegion ref{};
        bench::refill_meta_log(meta_log, trace);
        build_scratch.arena.with_epoch([&state, &log, &trace, numerics, &ref](TraceScratch::Epoch& epoch) noexcept {
            ref = build_recorded(state, log, trace, numerics, epoch).expect("the recorded trace builds").region;
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
        bench::refill_meta_log(meta_log, trace);
        build_scratch.arena.with_epoch([&state, &log, &trace, numerics, &ref](TraceScratch::Epoch& epoch) noexcept {
            ref = build_recorded(state, log, trace, numerics, epoch).expect("the recorded trace builds").region;
        });
        const ::fixy::View<TraceEntry const> ref_ops = ref.ops();

        for (uint32_t iter = 0; iter < iters; iter++) {
            BuildScratchOfBench::map_generation(state)++;
            if (BuildScratchOfBench::map_generation(state) == 0) {
                BuildScratchOfBench::clear_map(state);
                BuildScratchOfBench::map_generation(state) = 1;
            }
            const uint8_t local_gen = BuildScratchOfBench::map_generation(state);

            uint32_t i = 0;
            for (const TraceEntry& te : ref_ops) {
                uint8_t port = 0;
                for (const TensorMeta& m : te.output_metas()) {
                    if (m.data_ptr.value() != nullptr)
                        (void)BackgroundThread::ptr_map_insert(BuildScratchOfBench::map(state), local_gen,
                                                               crucible::storage_key(m), OpIndex{i}, port, SlotId{0});
                    ++port;
                }
                ++i;
            }

            const uint64_t t0 = bench::rdtsc_start();

            uint32_t dummy_edges = 0;
            for (const TraceEntry& te : ref_ops) {
                for (const TensorMeta& m : te.input_metas()) {
                    auto lookup = BackgroundThread::ptr_map_lookup(BuildScratchOfBench::map(state), local_gen,
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
        bench::refill_meta_log(meta_log, trace);
        build_scratch.arena.with_epoch([&state, &log, &trace, numerics, &ref](TraceScratch::Epoch& epoch) noexcept {
            ref = build_recorded(state, log, trace, numerics, epoch).expect("the recorded trace builds").region;
        });
        const ::fixy::View<TraceEntry const> ref_ops = ref.ops();

        for (uint32_t iter = 0; iter < iters; iter++) {
            BuildScratchOfBench::map_generation(state)++;
            if (BuildScratchOfBench::map_generation(state) == 0) {
                BuildScratchOfBench::clear_map(state);
                BuildScratchOfBench::map_generation(state) = 1;
            }
            const uint8_t local_gen = BuildScratchOfBench::map_generation(state);

            const uint64_t t0 = bench::rdtsc_start();

            uint32_t aliases = 0;
            uint32_t i = 0;
            for (const TraceEntry& te : ref_ops) {
                uint8_t port = 0;
                for (const TensorMeta& m : te.output_metas()) {
                    const uint8_t this_port = port++;
                    if (m.data_ptr.value() == nullptr) continue;
                    auto result =
                        BackgroundThread::ptr_map_insert(BuildScratchOfBench::map(state), local_gen,
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
        // The slots of a build, made from the slots of its region.  The epoch
        // of the build arena ends the scratch slots of the build itself.
        ::fixy::HeapArray<BackgroundThread::SlotInfo> saved_slots{};
        bench::refill_meta_log(meta_log, trace);
        build_scratch.arena.with_epoch([&state, &log, &trace, numerics,
                                        &saved_slots](TraceScratch::Epoch& epoch) noexcept {
            const BackgroundThread::BuiltRegion region =
                build_recorded(state, log, trace, numerics, epoch).expect("the recorded trace builds").region;
            const ::fixy::View<TensorSlot const> region_slots = region.slots();
            saved_slots = ::fixy::mint_heap_array_with<BackgroundThread::SlotInfo>(
                A, region_slots.size(), [region_slots](size_t position) noexcept {
                    TensorSlot const& slot = *region_slots.get(position).expect("the slot of each position is built");
                    return BackgroundThread::SlotInfo{.nbytes = slot.nbytes,
                                                      .birth_op = slot.birth_op,
                                                      .death_op = slot.death_op,
                                                      .dtype = slot.dtype,
                                                      .device_type = slot.device_type,
                                                      .device_idx = slot.device_idx,
                                                      .layout = slot.layout,
                                                      .is_external = slot.is_external,
                                                      .pad = {}};
                });
        });
        const ::fixy::View<BackgroundThread::SlotInfo const> built_slots = saved_slots.view();
        const size_t num_slots = built_slots.size();

        for (uint32_t iter = 0; iter < iters; iter++) {
            const uint64_t t0 = bench::rdtsc_start();

            // The run of the slots comes from an arena of the region, as
            // build_trace takes it.  The block holds the alignment of a slot,
            // a constant, plus the bytes of the slots.
            constexpr ::fixy::ArenaBlockSize kSlotAlignment{Bytes{alignof(TensorSlot)}};
            ::fixy::Box<::fixy::BumpArena<DagArena>> slot_arena = ::fixy::mint_box<::fixy::BumpArena<DagArena>>(
                A, kSlotAlignment.plus(Bytes{num_slots * sizeof(TensorSlot)}));
            auto slots_run = slot_arena->array(A, num_slots, TensorSlot{});
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<TensorSlot> slots = slots_run.view();
            uint32_t position = 0;
            for (const auto [info, slot] :
                 built_slots.walk().try_zip(slots.walk()).expect("the run of the slots holds each slot")) {
                *slot = TensorSlot{.offset_bytes = 0,
                                   .nbytes = info->nbytes,
                                   .birth_op = info->birth_op,
                                   .death_op = info->death_op,
                                   .dtype = info->dtype,
                                   .device_type = info->device_type,
                                   .device_idx = info->device_idx,
                                   .layout = info->layout,
                                   .is_external = info->is_external,
                                   .pad = {},
                                   .slot_id = SlotId{position},
                                   .pad2 = {}};
                ++position;
            }

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

    // The bench builds each trace from its own log, so no stage of the
    // BackgroundThread reads the log.  The thread holds the sides of a rig of
    // its own, which no stage reads either.
    crucible::test::OpenLog meta_log{};
    crucible::test::TraceRig sides{};

    BackgroundThread bg{::foundation::effects::testing::test().alloc, sides.ring_side(), sides.log_side()};

    // A frame holds the state of the build for a whole section, so no sample
    // of a section takes the claim of the run state.
    NumericsHash const numerics = bg.numerics();
    BackgroundThread::PlanTarget const target = bg.plan_target();
    bench::print("\n=== full pipeline (bench::run with BPF sensing) ===\n");
    ::fixy::FixedArray<bench::Report, 1> const reports{bg.with_trace_build_state(
        bg.prove_quiescence(), [&meta_log, &trace, numerics](BackgroundThread::TraceBuildState& state) noexcept {
            return run_fullpipeline(state, meta_log, trace, numerics);
        })};
    bench::emit_reports_text(reports.view());

    const auto run_phases = [target, &meta_log, &trace, numerics,
                             iters](BackgroundThread::TraceBuildState& state) noexcept {
        bench_phases_toplevel(state, target, meta_log, trace, numerics, iters);
        bench_phase2_subparts(state, meta_log, trace, numerics, iters);
    };
    bg.with_trace_build_state(bg.prove_quiescence(), run_phases);

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
