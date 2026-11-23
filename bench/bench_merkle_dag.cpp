// MerkleDag + BackgroundThread::build_trace hot-path benchmarks.
//
// Covers:
//   - compute_content_hash() at 3 region sizes (8, 481, 1110 ops)
//   - make_region() construction
//   - compute_merkle_hash() on a built RegionNode
//   - build_csr() adjacency construction
//   - compute_memory_plan() sweep-line
//   - build_trace_from() full pipeline (arena + PtrMap + slot tracking + CSR)
//   - optional: build_trace from loaded .crtrace file(s) (pass as argv)
//
// Simulates ResNet-18 (481 ops) and GPT-style (1110 ops) iteration sizes.

#include <cstdint>

#include <crucible/BackgroundThread.h>
#include <foundation/effects/Effect.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceLoader.h>
#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <fixy/os/Args.h>

#include "bench_harness.h"
#include "bench_trace.h"

using namespace crucible;

namespace {

// Deterministic xorshift PRNG, seeded per-bench for reproducibility.
uint64_t bench_rng_state = 0xDEADBEEFCAFEBABEULL;

uint64_t bench_rand() noexcept {
    bench_rng_state ^= bench_rng_state << 13;
    bench_rng_state ^= bench_rng_state >> 7;
    bench_rng_state ^= bench_rng_state << 17;
    return bench_rng_state;
}

[[noreturn]] CRUCIBLE_COLD void abort_full_ring(uint32_t appended, uint32_t total) noexcept {
    ::fixy::fatal("bench_merkle_dag: the TraceRing is full after {} of {} ops", appended, total);
}

// The content hash loads the full lane width of sizes and strides, and it
// masks the lanes past ndim.  The arena gives raw storage, so a meta that the
// setup does not construct holds the bytes that the arena held before.
void check_meta_tail(const TensorMeta& meta) {
    for (uint8_t d = meta.ndim; d < kMaxTensorNDim; d++)
        CRUCIBLE_BENCH_CHECK(raw_tensor_dim(meta.sizes[d]) == 0 && raw_tensor_dim(meta.strides[d]) == 0);
}

// One metadata of rank ndim with random sizes, strides and data pointer.
TensorMeta make_synthetic_meta(uint8_t ndim) {
    TensorMeta m{};
    m.ndim = ndim;
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CUDA;
    m.device_idx = 0;
    for (uint8_t d = 0; d < ndim; d++) {
        m.sizes[d] = tensor_dim(static_cast<int64_t>(32 + (bench_rand() % 256)));
        m.strides[d] = tensor_dim(static_cast<int64_t>(1 + (bench_rand() % 64)));
    }
    m.data_ptr = mint_synthetic_data_ptr(SyntheticAddress{0x7f0000000000ULL + bench_rand() % 0x100000});
    check_meta_tail(m);
    return m;
}

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

TraceEntry make_synthetic_entry(::foundation::effects::Alloc a, ::fixy::BumpArena<DagArena>& arena, uint16_t n_in,
                                uint16_t n_out, uint16_t n_scalar, uint8_t ndim) {
    TraceEntry te{};
    te.schema_hash = SchemaHash{bench_rand()};
    te.shape_hash = ShapeHash{bench_rand()};
    te.scope_hash = ScopeHash{bench_rand()};
    te.callsite_hash = CallsiteHash{bench_rand()};
    te.grad_enabled = true;
    te.kernel_id = CKernelId::OPAQUE;

    const auto input_metas_run = arena.array(a, n_in, TensorMeta{});
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<TensorMeta> input_metas = input_metas_run.view();
    for (TensorMeta& m : input_metas)
        m = make_synthetic_meta(ndim);
    const auto producers_run = arena.array(a, n_in, OpIndex{});
    const auto input_slots_run = arena.array(a, n_in, SlotId{});
    // A plain View of an arena run: the View carries no brand of its arena.
    te.set_inputs(input_metas, producers_run.view(), input_slots_run.view());
    const auto output_metas_run = arena.array(a, n_out, TensorMeta{});
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<TensorMeta> output_metas = output_metas_run.view();
    for (TensorMeta& m : output_metas)
        m = make_synthetic_meta(ndim);
    const auto output_slots_run = arena.array(a, n_out, SlotId{});
    // A plain View of an arena run: the View carries no brand of its arena.
    te.set_outputs(output_metas, output_slots_run.view());
    const auto scalars_run = arena.array(a, n_scalar, int64_t{0});
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<int64_t> scalars = scalars_run.view();
    for (int64_t& value : scalars)
        value = static_cast<int64_t>(bench_rand());
    te.set_scalar_args(scalars);
    return te;
}

// The bench with the arguments of main: the paths of the trace files after
// the name of the program.
int run_bench(::fixy::os::Args const& arguments) {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== merkle_dag ===\n\n");

    // The eight fixed rows, then one row for each trace file of argv.
    constexpr std::size_t kMaxTraceFiles = 56;
    if (arguments.size() > kMaxTraceFiles + 1) {
        ::fixy::fatal("bench_merkle_dag takes at most {} trace files", kMaxTraceFiles);
    }
    ::fixy::FixedList<bench::Report, 8 + kMaxTraceFiles> reports{};

    // ── compute_content_hash at three region sizes ───────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        constexpr uint32_t N = 8;
        ::foundation::core::FixedArray<TraceEntry, N> ops{};
        bench_rng_state = 0xDEADBEEFCAFEBABEULL;
        for (::fixy::Index<N> const index : ops.indices())
            ops.with_element(index, [&test, &arena](TraceEntry& op) noexcept {
                op = make_synthetic_entry(test.alloc, arena, 2, 1, 2, 4);
            });
        return bench::run("content_hash (8 ops, 2in/1out, 4d)", [&] {
            auto h = compute_content_hash(ops.view());
            bench::do_not_optimize(h);
        });
    }());

    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        ::fixy::BumpArena<DagArena> ops_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 481;
        const auto ops_run = ops_arena.array<TraceEntry>(test.alloc, N, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();
        bench_rng_state = 0x12345678ABCDEF01ULL;
        for (uint32_t i = 0; i < N; i++) {
            const uint16_t n_in = static_cast<uint16_t>(1 + (bench_rand() % 3));
            const uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
            *ops.get(i).expect("i is below N") = make_synthetic_entry(test.alloc, arena, n_in, n_out, 1, 4);
        }
        return bench::run("content_hash (481 ops, ResNet-like)", [&] {
            auto h = compute_content_hash(ops);
            bench::do_not_optimize(h);
        });
    }());

    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        ::fixy::BumpArena<DagArena> ops_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 1110;
        const auto ops_run = ops_arena.array<TraceEntry>(test.alloc, N, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();
        bench_rng_state = 0xABCDEF0123456789ULL;
        for (uint32_t i = 0; i < N; i++) {
            const uint16_t n_in = static_cast<uint16_t>(1 + (bench_rand() % 4));
            const uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
            *ops.get(i).expect("i is below N") = make_synthetic_entry(test.alloc, arena, n_in, n_out, 2, 4);
        }
        return bench::run("content_hash (1110 ops, GPT-like)", [&] {
            auto h = compute_content_hash(ops);
            bench::do_not_optimize(h);
        });
    }());

    // ── make_region (481 ops) ────────────────────────────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> data_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        ::fixy::BumpArena<DagArena> ops_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 481;
        const auto ops_run = ops_arena.array<TraceEntry>(test.alloc, N, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();
        bench_rng_state = 0x12345678ABCDEF01ULL;
        for (uint32_t i = 0; i < N; i++)
            *ops.get(i).expect("i is below N") = make_synthetic_entry(test.alloc, data_arena, 2, 1, 1, 4);
        return bench::run("make_region (481 ops)", [&] {
            ::fixy::BumpArena<DagArena> region_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
            const ::fixy::Ref<RegionNode> r = make_region(test.alloc, region_arena, ops);
            bench::do_not_optimize(r);
        });
    }());

    // ── compute_merkle_hash ──────────────────────────────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        ::fixy::BumpArena<DagArena> dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 481;
        const auto ops_run = dag_arena.array<TraceEntry>(test.alloc, N, TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();
        bench_rng_state = 0xAAAABBBBCCCCDDDDULL;
        for (uint32_t i = 0; i < N; i++)
            *ops.get(i).expect("i is below N") = make_synthetic_entry(test.alloc, arena, 2, 1, 1, 4);
        const ::fixy::Ref<RegionNode> region = make_region(test.alloc, dag_arena, ops);
        return bench::run("compute_merkle_hash (RegionNode)", [&] {
            auto h = compute_merkle_hash(*region);
            bench::do_not_optimize(h);
        });
    }());

    // ── build_csr (481 ops, 900 edges) ────────────────────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        constexpr uint32_t NUM_OPS = 481;
        constexpr uint32_t NUM_EDGES = 900;
        // The arena of the edges ends with this scope, so the report leaves
        // the scope through a variable.
        ::fixy::Option<bench::Report> report{};
        ::fixy::with_bump_arena(test.alloc, Bytes{size_t{1} << 20}, [&](auto& arena) noexcept {
            const auto edges_run = arena.array(test.alloc, NUM_EDGES, Edge{});
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<Edge> edges = edges_run.view();
            bench_rng_state = 0x5555666677778888ULL;
            for (Edge& edge : edges) {
                const uint32_t src = static_cast<uint32_t>(bench_rand() % NUM_OPS);
                const uint32_t dst = ::fixy::min(src + 1 + static_cast<uint32_t>(bench_rand() % 5), NUM_OPS - 1);
                edge = Edge{.src = OpIndex{src},
                            .dst = OpIndex{dst},
                            .src_port = 0,
                            .dst_port = 0,
                            .kind = EdgeKind::DATA_FLOW,
                            .pad = 0};
            }
            report = ::fixy::Option<bench::Report>::some(bench::run("build_csr (481 ops, 900 edges)", [&] {
                TraceScratch csr_scratch{Bytes{size_t{1} << 16}};
                csr_scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
                    const ::fixy::Ref<TraceGraph> graph = alloc_trace_graph(test.alloc, epoch);
                    build_csr(test.alloc, epoch, *graph, edges, NUM_OPS);
                    bench::do_not_optimize(graph);
                });
            }));
        });
        return std::move(report).expect("the scope of the edges ran the bench");
    }());

    // ── compute_memory_plan (300 slots) ──────────────────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        constexpr uint32_t NUM_SLOTS = 300;
        ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, NUM_SLOTS, TensorSlot{});
        bench_rng_state = 0x9999AAAABBBBCCCCULL;
        slots = ::fixy::with_mut(std::move(slots), [](auto run) noexcept {
            uint32_t slot_index = 0;
            for (TensorSlot& slot : run) {
                slot.slot_id = SlotId{slot_index};
                slot.birth_op = OpIndex{static_cast<uint32_t>(bench_rand() % 400)};
                slot.death_op = OpIndex{slot.birth_op.raw() + static_cast<uint32_t>(1 + bench_rand() % 80)};
                slot.nbytes = 256 * (1 + bench_rand() % 64);
                slot.dtype = ScalarType::Float;
                slot.device_type = DeviceType::CUDA;
                slot.device_idx = 0;
                slot.is_external = (slot_index < 20);  // first 20 are params
                // The planner writes the offset of each internal slot only, so
                // each external slot keeps the zero of its member initializer.
                CRUCIBLE_BENCH_CHECK(slot.offset_bytes == 0);
                ++slot_index;
            }
        });
        BackgroundThread bg{::foundation::effects::testing::test().alloc};
        // The planner writes the offsets into the slots, so each run plans a
        // copy of them in an arena of its own.  The arrays of the sweep are in
        // sweep_scratch.
        PlanScratch sweep_scratch{Bytes{size_t{1} << 20}};
        return bench::run("compute_memory_plan (300 slots)", [&] {
            ::fixy::BumpArena<DagArena> slot_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
            const auto copy_run = slot_arena.array<TensorSlot>(test.alloc, NUM_SLOTS, TensorSlot{});
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<TensorSlot> copy = copy_run.view();
            ::fixy::copy(copy, slots.view()).expect("the copy holds one entry for each slot");
            MemoryPlan plan_storage{};
            BackgroundThread::PlanTarget const target = bg.plan_target();
            sweep_scratch.arena.with_epoch(
                [target, alloc = test.alloc, &plan_storage, copy](PlanScratch::Epoch& epoch) noexcept {
                    const ::fixy::Ref<MemoryPlan> plan =
                        BackgroundThread::compute_memory_plan(alloc, target, plan_storage, epoch, copy)
                            .expect("the slots of the bench fit in 64 bits");
                    bench::do_not_optimize(plan);
                });
        });
    }());

    // ── build_trace full pipeline (481 synthetic ops) ────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();

        // A TraceRing is 6 MiB and the stack is 8 MiB, so the ring lives
        // on the heap, as it does in Vigil.
        ::fixy::Box<TraceRing> ring_box = ::fixy::mint_box<TraceRing>(test.alloc);
        TraceRing& ring = *ring_box;
        ring.reset();
        MetaLog meta_log{::foundation::effects::testing::test().alloc};
        meta_log.reset();

        bench_rng_state = 0x1111222233334444ULL;
        constexpr uint32_t N = 481;

        for (uint32_t i = 0; i < N; i++) {
            TraceRing::Entry e{};
            e.schema_hash = SchemaHash{bench_rand()};
            e.shape_hash = ShapeHash{bench_rand()};
            e.num_inputs = static_cast<uint16_t>(1 + (bench_rand() % 3));
            e.num_outputs = static_cast<uint16_t>(1 + (bench_rand() % 2));
            e.num_scalar_args = static_cast<uint8_t>(bench_rand() % 3);
            e.op_flags = static_cast<uint8_t>(e.op_flags | op_flag::GRAD_ENABLED);

            const uint16_t total_metas = static_cast<uint16_t>(e.num_inputs + e.num_outputs);
            ::fixy::FixedArray<TensorMeta, 8> metas{};
            for (uint16_t j = 0; j < total_metas && j < 8; j++) {
                metas.with_element(::fixy::Index<8>::check(j).expect("the position fits the array"),
                                   [i, j](TensorMeta& meta) noexcept {
                                       meta.ndim = 4;
                                       meta.dtype = ScalarType::Float;
                                       meta.device_type = DeviceType::CUDA;
                                       meta.device_idx = 0;
                                       for (uint8_t d = 0; d < 4; d++) {
                                           meta.sizes[d] = tensor_dim(static_cast<int64_t>(32 + (bench_rand() % 256)));
                                           meta.strides[d] = tensor_dim(static_cast<int64_t>(1 + (bench_rand() % 64)));
                                       }
                                       meta.data_ptr = mint_synthetic_data_ptr(
                                           SyntheticAddress{0x7f0000000000ULL + (i * 8 + j) * 0x1000});
                                   });
            }
            MetaIndex meta_start = meta_log.try_append(
                ::fixy::View<const TensorMeta>{metas.view()}.window(0, total_metas).expect("the run fits the array"));
            if (!ring.try_append(e, meta_start, ScopeHash{bench_rand()}, CallsiteHash{bench_rand()})) [[unlikely]]
                abort_full_ring(i, N);
        }

        BackgroundThread bg{::foundation::effects::testing::test().alloc};
        bg.bind_ring(ring);
        bg.bind_meta_log(meta_log);

        // The drain buffers have static storage; 4× 4096-element arrays on
        // the stack blow past our 512 kB stack-usage budget.
        static ::fixy::FixedArray<TraceRing::Entry, 4096> batch{};
        static ::fixy::FixedArray<MetaIndex, 4096> meta_batch{};
        static ::fixy::FixedArray<ScopeHash, 4096> scope_batch{};
        static ::fixy::FixedArray<CallsiteHash, 4096> callsite_batch{};
        static ::fixy::FixedArray<OpDepth, 4096> depth_batch{};
        // The first n entries of the five batches are the drained trace.  No
        // later code writes the batches.
        const uint32_t n = batch.with_mut([&](auto batch_run) noexcept {
            return meta_batch.with_mut([&](auto meta_run) noexcept {
                return scope_batch.with_mut([&](auto scope_run) noexcept {
                    return callsite_batch.with_mut([&](auto callsite_run) noexcept {
                        return depth_batch.with_mut([&](auto depth_run) noexcept {
                            return ring.drain(batch_run.view(), meta_run.view(), scope_run.view(), callsite_run.view(),
                                              depth_run.view());
                        });
                    });
                });
            });
        });

        auto repopulate_meta_log = [&] {
            meta_log.reset();
            bench_rng_state = 0x1111222233334444ULL;
            for (uint32_t i = 0; i < n; i++) {
                const TraceRing::Entry& entry =
                    batch.at(::fixy::Index<4096>::check(i).expect("the drain fills at most the batch"));
                const uint16_t total_metas = static_cast<uint16_t>(entry.num_inputs + entry.num_outputs);
                ::fixy::FixedArray<TensorMeta, 8> metas{};
                for (uint16_t j = 0; j < total_metas && j < 8; j++) {
                    metas.with_element(::fixy::Index<8>::check(j).expect("the position fits the array"),
                                       [i, j](TensorMeta& meta) noexcept {
                                           meta.ndim = 4;
                                           meta.dtype = ScalarType::Float;
                                           meta.device_type = DeviceType::CUDA;
                                           meta.data_ptr = mint_synthetic_data_ptr(
                                               SyntheticAddress{0x7f0000000000ULL + (i * 8 + j) * 0x1000});
                                       });
                }
                // The build reads the metas at the indices in meta_batch, so this call does not
                // use the returned index.
                (void)meta_log.try_append(::fixy::View<const TensorMeta>{metas.view()}
                                              .window(0, total_metas)
                                              .expect("the run fits the array"));
            }
        };

        // The build reads the first n entries of each batch.
        const ::fixy::View<const TraceRing::Entry> drained =
            batch.view().window(0, n).expect("the drain fills at most the batch");
        const ::fixy::View<const MetaIndex> drained_metas =
            meta_batch.view().window(0, n).expect("the drain fills at most the batch");
        const ::fixy::View<const ScopeHash> drained_scopes =
            scope_batch.view().window(0, n).expect("the drain fills at most the batch");
        const ::fixy::View<const CallsiteHash> drained_callsites =
            callsite_batch.view().window(0, n).expect("the drain fills at most the batch");
        const ::fixy::View<const OpDepth> drained_depths =
            depth_batch.view().window(0, n).expect("the drain fills at most the batch");

        TraceScratch scratch{Bytes{size_t{1} << 20}};
        // No pipeline of the bench runs, so the proof stays valid for each
        // iteration.  The body captures the state of the build and the
        // metadata log by a const reference, and not the BackgroundThread.
        BackgroundThread::TraceBuildState& state = bg.trace_build_state(bg.prove_quiescence());
        MetaLog const& log = meta_log;
        NumericsHash const numerics = bg.numerics();
        return bench::run("build_trace (481 ops, full pipeline)", [&] {
            repopulate_meta_log();
            scratch.arena.with_epoch([&state, &log, alloc = test.alloc, drained, drained_metas, drained_scopes,
                                      drained_callsites, drained_depths,
                                      numerics](TraceScratch::Epoch& epoch) noexcept {
                auto built =
                    BackgroundThread::build_trace_from(state, log, alloc, epoch, drained, drained_metas, drained_scopes,
                                                       drained_callsites, drained_depths, numerics);
                bench::do_not_optimize(built);
            });
        });
    }());

    // ── Optional: build_trace from real .crtrace argv paths ──────────
    for (std::size_t position = 1; position < arguments.size(); ++position) {
        const ::fixy::CText path = arguments.get(position).expect("a position below the count holds an argument");
        bench::add_report(reports, [path_view = path.view()] {
            auto test = ::foundation::effects::testing::test();
            // A path that fails the path check counts as a file that does not open.
            auto sanitized_path = ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(path_view));
            auto loaded_trace =
                sanitized_path.is_ok()
                    ? load_trace(::fixy::TestRunnerCtx{test},
                                 std::move(sanitized_path).expect("the path passed the check"))
                    : ::fixy::Result<LoadedTrace, TraceLoadError>{::fixy::err(TraceLoadError::OpenFailed)};
            // A trace that does not load gives a row with no sample, so the
            // table shows the skip and no timed loop runs an empty body.
            if (loaded_trace.is_err()) {
                ::fixy::report(::fixy::Sink::Err, "[skip] could not load {}\n", path_view);
                bench::Report skipped{};
                skipped.name = bench::name_of("build_trace (skipped — load fail)");
                return skipped;
            }
            const LoadedTrace trace =
                std::move(loaded_trace).expect("the load holds a trace after the check of its error");
            ::fixy::report(::fixy::Sink::Err, "[trace] {}: {} ops, {} metas\n", path_view, trace.num_ops,
                           trace.num_metas);

            MetaLog meta_log{::foundation::effects::testing::test().alloc};
            meta_log.reset();

            BackgroundThread bg{::foundation::effects::testing::test().alloc};
            bg.bind_meta_log(meta_log);
            const size_t arena_bytes = ::fixy::max(size_t{1} << 20, static_cast<size_t>(trace.num_ops) * 256);
            TraceScratch scratch{Bytes{arena_bytes}};
            // The body captures the state of the build and the metadata log
            // by a const reference, and not the BackgroundThread.
            BackgroundThread::TraceBuildState& state = bg.trace_build_state(bg.prove_quiescence());
            MetaLog const& log = meta_log;
            NumericsHash const numerics = bg.numerics();

            return bench::run(bench::format_name("build_trace ({} ops, from {})", trace.num_ops, path_view), [&] {
                const LoadedTrace& loaded = trace;
                bench::refill_meta_log(meta_log, loaded);
                scratch.arena.with_epoch(
                    [&state, &log, alloc = test.alloc, &loaded, numerics](TraceScratch::Epoch& epoch) noexcept {
                        auto built = BackgroundThread::build_trace_from(
                            state, log, alloc, epoch, loaded.entries.view(), loaded.meta_starts.view(),
                            loaded.scope_hashes.view(), loaded.callsite_hashes.view(), loaded.depths.view(), numerics);
                        bench::do_not_optimize(built);
                    });
            });
        }());
    }

    bench::emit_reports_text(reports.view());

    // Scaling check: content_hash goes 8 → 481 → 1110 ops.
    // Expected roughly linear since every op does the same work.
    const bench::Report& hash_8 = *reports.get(0).expect("the content_hash row of 8 ops");
    const bench::Report& hash_481 = *reports.get(1).expect("the content_hash row of 481 ops");
    bench::print("\n=== compare — content_hash scaling ===\n");
    bench::print("  481 / 8   (expect ~60×):\n  ");
    bench::compare(hash_8, hash_481).print_text();
    bench::print("  1110 / 481 (expect ~2.3×):\n  ");
    bench::compare(hash_481, *reports.get(2).expect("the content_hash row of 1110 ops")).print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return run_bench(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
