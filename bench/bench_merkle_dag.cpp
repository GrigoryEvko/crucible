// MerkleDag + BackgroundThread::build_trace hot-path benchmarks.
//
// Covers:
//   - compute_content_hash() at 3 region sizes (8, 481, 1110 ops)
//   - make_region() construction
//   - compute_merkle_hash() on a built RegionNode
//   - build_trace_graph() adjacency construction
//   - compute_memory_plan() sweep-line
//   - build_trace_from() full pipeline (arena + PtrMap + slot tracking + CSR)
//   - optional: build_trace from loaded .crtrace file(s) (pass as argv)
//
// Simulates ResNet-18 (481 ops) and GPT-style (1110 ops) iteration sizes.

#include <cstdint>
#include <utility>

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

// Fills the op at position of the run with random hashes, and with n_in
// inputs, n_out outputs and n_scalar scalars in the arena of the frame.
template <class Fresh>
void fill_synthetic_entry(::foundation::effects::Alloc a, DagFrame<Fresh>& frame,
                          DagFrameRun<TraceEntry, Fresh>& ops_run, std::size_t position, uint16_t n_in, uint16_t n_out,
                          uint16_t n_scalar, uint8_t ndim) {
    const auto op = hold_op(ops_run, position);
    TraceEntry& te = *op;
    te.schema_hash = SchemaHash{bench_rand()};
    te.shape_hash = ShapeHash{bench_rand()};
    te.scope_hash = ScopeHash{bench_rand()};
    te.callsite_hash = CallsiteHash{bench_rand()};
    te.grad_enabled = true;
    te.kernel_id = CKernelId::OPAQUE;

    auto input_metas_run = frame.array(a, n_in, TensorMeta{});
    for (TensorMeta& m : input_metas_run.view())
        m = make_synthetic_meta(ndim);
    TraceEntry::set_inputs(frame, op, input_metas_run, frame.array(a, n_in, OpIndex{}), frame.array(a, n_in, SlotId{}));
    auto output_metas_run = frame.array(a, n_out, TensorMeta{});
    for (TensorMeta& m : output_metas_run.view())
        m = make_synthetic_meta(ndim);
    TraceEntry::set_outputs(frame, op, output_metas_run, frame.array(a, n_out, SlotId{}));
    auto scalars_run = frame.array(a, n_scalar, int64_t{0});
    for (int64_t& value : scalars_run.view())
        value = static_cast<int64_t>(bench_rand());
    TraceEntry::set_scalar_args(frame, op, scalars_run);
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
        bench_rng_state = 0xDEADBEEFCAFEBABEULL;
        // One frame on the arena builds the ops and their arrays.
        const ::fixy::ArenaRun<TraceEntry const, DagArena> ops_run =
            ::foundation::core::with_arena_frame(arena, [&test](auto& frame) noexcept {
                auto made = make_op_run(test.alloc, frame, N);
                for (uint32_t i = 0; i < N; i++)
                    fill_synthetic_entry(test.alloc, frame, made, i, 2, 1, 2, 4);
                const auto& built = made;
                return built.weaken();
            });
        const ::fixy::View<TraceEntry const> ops = ops_run.view();
        return bench::run("content_hash (8 ops, 2in/1out, 4d)", [&] {
            auto h = compute_content_hash(ops);
            bench::do_not_optimize(h);
        });
    }());

    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> ops_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 481;
        bench_rng_state = 0x12345678ABCDEF01ULL;
        // One frame on the arena builds the ops and their arrays.
        const ::fixy::ArenaRun<TraceEntry const, DagArena> ops_run =
            ::foundation::core::with_arena_frame(ops_arena, [&test](auto& frame) noexcept {
                auto made = make_op_run(test.alloc, frame, N);
                for (uint32_t i = 0; i < N; i++) {
                    const uint16_t n_in = static_cast<uint16_t>(1 + (bench_rand() % 3));
                    const uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
                    fill_synthetic_entry(test.alloc, frame, made, i, n_in, n_out, 1, 4);
                }
                const auto& built = made;
                return built.weaken();
            });
        const ::fixy::View<TraceEntry const> ops = ops_run.view();
        return bench::run("content_hash (481 ops, ResNet-like)", [&] {
            auto h = compute_content_hash(ops);
            bench::do_not_optimize(h);
        });
    }());

    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> ops_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 1110;
        bench_rng_state = 0xABCDEF0123456789ULL;
        // One frame on the arena builds the ops and their arrays.
        const ::fixy::ArenaRun<TraceEntry const, DagArena> ops_run =
            ::foundation::core::with_arena_frame(ops_arena, [&test](auto& frame) noexcept {
                auto made = make_op_run(test.alloc, frame, N);
                for (uint32_t i = 0; i < N; i++) {
                    const uint16_t n_in = static_cast<uint16_t>(1 + (bench_rand() % 4));
                    const uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
                    fill_synthetic_entry(test.alloc, frame, made, i, n_in, n_out, 2, 4);
                }
                const auto& built = made;
                return built.weaken();
            });
        const ::fixy::View<TraceEntry const> ops = ops_run.view();
        return bench::run("content_hash (1110 ops, GPT-like)", [&] {
            auto h = compute_content_hash(ops);
            bench::do_not_optimize(h);
        });
    }());

    // ── make_region (481 ops) ────────────────────────────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> ops_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 481;
        // One frame on the arena of the ops builds each region of the row, so
        // each region takes the ops of its own arena.
        return ::foundation::core::with_arena_frame(ops_arena, [&test](auto& frame) noexcept {
            auto ops_run = make_op_run(test.alloc, frame, N);
            bench_rng_state = 0x12345678ABCDEF01ULL;
            for (uint32_t i = 0; i < N; i++)
                fill_synthetic_entry(test.alloc, frame, ops_run, i, 2, 1, 1, 4);
            return bench::run("make_region (481 ops)", [&] {
                const DagRef<RegionNode> r = make_region(test.alloc, frame, ops_run).weaken();
                bench::do_not_optimize(r);
            });
        });
    }());

    // ── compute_merkle_hash ──────────────────────────────────────────
    bench::add_report(reports, [] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        constexpr uint32_t N = 481;
        const DagRef<RegionNode> region =
            ::foundation::core::with_arena_frame(dag_arena, [&test](auto& frame) noexcept {
                auto ops_run = make_op_run(test.alloc, frame, N);
                bench_rng_state = 0xAAAABBBBCCCCDDDDULL;
                for (uint32_t i = 0; i < N; i++)
                    fill_synthetic_entry(test.alloc, frame, ops_run, i, 2, 1, 1, 4);
                return make_region(test.alloc, frame, ops_run).weaken();
            });
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
            auto edges_run = arena.array(test.alloc, NUM_EDGES, Edge{});
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
                    const TraceGraphRef graph =
                        build_trace_graph(test.alloc, epoch, TraceGraphShape{.num_ops = NUM_OPS}, edges);
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
        // The planner writes the offsets into the slots, so each run plans a
        // copy of them in an arena of its own.  The arrays of the sweep are in
        // sweep_scratch.
        PlanScratch sweep_scratch{Bytes{size_t{1} << 20}};
        return bench::run("compute_memory_plan (300 slots)", [&] {
            ::fixy::BumpArena<DagArena> slot_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
            auto copy_run = slot_arena.array<TensorSlot>(test.alloc, NUM_SLOTS, TensorSlot{});
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<TensorSlot> copy = copy_run.view();
            ::fixy::copy(copy, slots.view()).expect("the copy holds one entry for each slot");
            // The plan is in the arena of the slots, so it ends with them.
            const DagRef<MemoryPlan> plan_storage = slot_arena.make<MemoryPlan>(test.alloc);
            BackgroundThread::PlanTarget const target{};
            sweep_scratch.arena.with_epoch(
                [target, alloc = test.alloc, plan_storage, &copy_run](PlanScratch::Epoch& epoch) noexcept {
                    const DagRef<MemoryPlan> plan =
                        BackgroundThread::compute_memory_plan(alloc, target, plan_storage, epoch, copy_run)
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
        // Each door of the ring and of the log of the bench opens the side of
        // the door for one call (test/trace_rig.h).
        ::fixy::Box<crucible::test::OpenRing> ring_box = ::fixy::mint_box<crucible::test::OpenRing>(test.alloc);
        crucible::test::OpenRing& ring = *ring_box;
        crucible::test::OpenLog meta_log{};

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
                metas.update(::fixy::Index<8>::check(j).expect("the position fits the array"),
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

        // The bench drains the ring and builds the trace itself, so no stage
        // of the BackgroundThread reads the ring or the log.  The thread
        // holds the sides of a rig of its own, which no stage reads either.
        crucible::test::TraceRig sides{};

        // The drain buffers have static storage; 4× 4096-element arrays on
        // the stack blow past our 512 kB stack-usage budget.
        auto [batch, batch_key] = ::fixy::mint_keyed_owner(::fixy::fixed_heap_array_owner<TraceRing::Entry, 4096>,
                                                           test.alloc, TraceRing::Entry{});
        auto [meta_batch, meta_key] =
            ::fixy::mint_keyed_owner(::fixy::fixed_heap_array_owner<MetaIndex, 4096>, test.alloc, MetaIndex{});
        auto [scope_batch, scope_key] =
            ::fixy::mint_keyed_owner(::fixy::fixed_heap_array_owner<ScopeHash, 4096>, test.alloc, ScopeHash{});
        auto [callsite_batch, callsite_key] =
            ::fixy::mint_keyed_owner(::fixy::fixed_heap_array_owner<CallsiteHash, 4096>, test.alloc, CallsiteHash{});
        auto [depth_batch, depth_key] =
            ::fixy::mint_keyed_owner(::fixy::fixed_heap_array_owner<OpDepth, 4096>, test.alloc, OpDepth{});
        // The first n entries of the five batches are the drained trace.  No
        // later code writes the batches.
        const uint32_t n = batch.with_mut(batch_key, [&](auto batch_run) noexcept {
            return meta_batch.with_mut(meta_key, [&](auto meta_run) noexcept {
                return scope_batch.with_mut(scope_key, [&](auto scope_run) noexcept {
                    return callsite_batch.with_mut(callsite_key, [&](auto callsite_run) noexcept {
                        return depth_batch.with_mut(depth_key, [&](auto depth_run) noexcept {
                            return ring.drain(std::move(batch_run), std::move(meta_run), std::move(scope_run),
                                              std::move(callsite_run), std::move(depth_run));
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
                    metas.update(::fixy::Index<8>::check(j).expect("the position fits the array"),
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
        // One frame holds the state of the build for each iteration.  The
        // bodies capture the metadata log by a const reference, and not the
        // BackgroundThread.
        MetaLog const& log = *meta_log.view();
        bench::Report report{};
        crucible::with_background_thread(
            test.alloc, sides.ring_side(), sides.log_side(), {},
            [&](BackgroundThread const& /*bg*/, auto stopped) noexcept {
                NumericsHash const numerics = stopped.numerics();
                const auto measure = [&repopulate_meta_log, &scratch, &log, &test, drained, drained_metas,
                                      drained_scopes, drained_callsites, drained_depths,
                                      numerics](BackgroundThread::TraceBuildState& state) noexcept {
                    return bench::run("build_trace (481 ops, full pipeline)", [&] {
                        repopulate_meta_log();
                        scratch.arena.with_epoch([&state, &log, alloc = test.alloc, drained, drained_metas,
                                                  drained_scopes, drained_callsites, drained_depths,
                                                  numerics](TraceScratch::Epoch& epoch) noexcept {
                            auto built = BackgroundThread::build_trace_from(
                                state, log, alloc, epoch, drained, drained_metas, drained_scopes, drained_callsites,
                                drained_depths, numerics);
                            bench::do_not_optimize(built);
                        });
                    });
                };
                report = stopped.with_trace_build_state(measure);
            });
        return report;
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

            crucible::test::OpenLog meta_log{};

            // The thread holds the sides of a rig of its own, which no stage
            // reads: the bench builds each trace from its own log.
            crucible::test::TraceRig sides{};
            // The block takes 256 bytes for each op, and at least 1 MiB.  The
            // floor is a constant, and the bytes of the ops above the floor
            // are the part of run time.
            constexpr size_t kFloorBytes = size_t{1} << 20;
            constexpr ::fixy::ArenaBlockSize kFloor{Bytes{kFloorBytes}};
            const size_t op_bytes = static_cast<size_t>(trace.num_ops) * 256;
            TraceScratch scratch{kFloor.plus(Bytes{op_bytes - ::fixy::min(op_bytes, kFloorBytes)})};
            // The bodies capture the metadata log by a const reference, and
            // not the BackgroundThread.
            MetaLog const& log = *meta_log.view();
            bench::Report report{};
            crucible::with_background_thread(
                test.alloc, sides.ring_side(), sides.log_side(), {},
                [&](BackgroundThread const& /*bg*/, auto stopped) noexcept {
                    NumericsHash const numerics = stopped.numerics();
                    const auto measure = [&meta_log, &log, &scratch, &trace, &test, path_view,
                                          numerics](BackgroundThread::TraceBuildState& state) noexcept {
                        const auto name = bench::format_name("build_trace ({} ops, from {})", trace.num_ops, path_view);
                        return bench::run(name, [&] {
                            const LoadedTrace& loaded = trace;
                            bench::refill_meta_log(meta_log, loaded);
                            scratch.arena.with_epoch([&state, &log, alloc = test.alloc, &loaded,
                                                      numerics](TraceScratch::Epoch& epoch) noexcept {
                                auto built = BackgroundThread::build_trace_from(
                                    state, log, alloc, epoch, loaded.entries.view(), loaded.meta_starts.view(),
                                    loaded.scope_hashes.view(), loaded.callsite_hashes.view(), loaded.depths.view(),
                                    numerics);
                                bench::do_not_optimize(built);
                            });
                        });
                    };
                    report = stopped.with_trace_build_state(measure);
                });
            return report;
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
