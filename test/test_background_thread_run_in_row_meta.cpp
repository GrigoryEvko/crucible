// The release of the metadata log in test_background_thread_run_in_row, and
// the copy of its records that a built graph holds.
//
// The foreground appends the tensor metadata of each op to the metadata log.
// Each commit marker of the pipeline carries a release floor: the oldest
// record that a later work can hold.  The publish stage moves the tail to the
// floor of each marker, and it is the one thread that writes the tail.  The
// publish of a region and a build that meets an overflow move no tail.

#include "background_thread_run_in_row.h"
#include "trace_rig.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CKernel.h>
#include <crucible/IterationDetector.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

using crucible::BackgroundThread;
using crucible::BuiltRegion;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::StreamPoint;
using crucible::TraceRing;

namespace meta_rig {
namespace {

// The metadata of one output tensor.  Each op gets a data pointer of its own,
// so the build sees no alias.
[[nodiscard]] crucible::TensorMeta output_meta(uint32_t op) noexcept {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(64);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    meta.data_ptr = crucible::external_data_ptr(std::bit_cast<void*>(std::uintptr_t{0x100000} + op * 0x1000ULL));
    return meta;
}

using BuildState = BackgroundThread::TraceBuildState;

// One work item for build_trace_from: the parallel arrays of a run of ops.
struct Work {
    static constexpr uint32_t CAPACITY = 8;
    uint32_t count = 0;
    ::fixy::FixedArray<TraceRing::Entry, CAPACITY> trace{};
    ::fixy::FixedArray<MetaIndex, CAPACITY> meta_starts{};
    ::fixy::FixedArray<crucible::ScopeHash, CAPACITY> scope_hashes{};
    ::fixy::FixedArray<crucible::CallsiteHash, CAPACITY> callsite_hashes{};
    ::fixy::FixedArray<crucible::OpDepth, CAPACITY> depths{};

    // The graph of the count ops of the work, in the epoch, with the state of
    // the build of a thread that runs no pipeline (with_trace_build_state).
    // The build reads the first count elements of each array.
    [[nodiscard]] BackgroundThread::TraceBuild build(BackgroundThread::TraceBuildState& state, MetaLog const& log,
                                                     crucible::NumericsHash numerics,
                                                     ::foundation::effects::Alloc alloc,
                                                     crucible::TraceScratch::Epoch& scratch) const {
        return BackgroundThread::build_trace_from(state, log, alloc, scratch, first_ops(trace), first_ops(meta_starts),
                                                  first_ops(scope_hashes), first_ops(callsite_hashes),
                                                  first_ops(depths), numerics);
    }

    // The region of the work, which the handle of a thread that runs no
    // pipeline builds from the log.
    template <class Stopped>
    [[nodiscard]] BuiltRegion build_region(Stopped& stopped, MetaLog const& log) const {
        crucible::NumericsHash const numerics = stopped.numerics();
        const auto build_in_frame = [this, &log, numerics](BuildState& state) noexcept {
            const auto test = ::foundation::effects::testing::test();
            crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
            ::fixy::Option<BuiltRegion> built{};
            scratch.arena.with_epoch([this, &state, &log, numerics, alloc = test.alloc,
                                      &built](crucible::TraceScratch::Epoch& epoch) noexcept {
                built = ::fixy::Option<BuiltRegion>::some(
                    build(state, log, numerics, alloc, epoch).expect("the work builds a graph").region);
            });
            return std::move(built).expect("the epoch built the region");
        };
        return stopped.with_trace_build_state(build_in_frame);
    }

    // The first count elements of one array of the work.
    template <class T>
    [[nodiscard]] ::fixy::View<const T> first_ops(::fixy::FixedArray<T, CAPACITY> const& column
                                                  CRUCIBLE_LIFETIMEBOUND) const {
        return column.view().window(0, count).expect("a work holds at most CAPACITY ops");
    }
    template <class T>
    void first_ops(::fixy::FixedArray<T, CAPACITY> const&&) const =
        delete("the View borrows the column, and a temporary ends with the full expression");

    // An op with one output tensor, whose metadata goes to the log.  Returns
    // the end of that metadata in the log.
    uint64_t push_op_with_meta(crucible::test::RigLog& log, uint32_t op) {
        CRUCIBLE_TEST_ASSERT(kTest, count < CAPACITY);
        const ::fixy::FixedArray<crucible::TensorMeta, 1> record{output_meta(op)};
        const MetaIndex index = log.append_metas(record.view());
        CRUCIBLE_TEST_ASSERT(kTest, index.is_valid());
        ::fixy::Index<CAPACITY> const position =
            ::fixy::Index<CAPACITY>::check(count).expect("a test reads an element at or past the count of its array");
        trace.update(position, [op](TraceRing::Entry& entry) noexcept {
            entry.schema_hash = crucible::SchemaHash{0x7000ULL + op};
            entry.shape_hash = crucible::ShapeHash{0x8000ULL + op};
            entry.num_outputs = 1;
        });
        meta_starts.set(position, index);
        ++count;
        return index.raw() + 1;
    }

    // An op with no tensor and two scalar arguments.  The foreground gives
    // such an op no metadata index.
    void push_op_with_scalars(uint32_t op, int64_t first, int64_t second) {
        CRUCIBLE_TEST_ASSERT(kTest, count < CAPACITY);
        ::fixy::Index<CAPACITY> const position =
            ::fixy::Index<CAPACITY>::check(count).expect("a test reads an element at or past the count of its array");
        trace.update(position, [op, first, second](TraceRing::Entry& entry) noexcept {
            entry.schema_hash = crucible::SchemaHash{0x7000ULL + op};
            entry.shape_hash = crucible::ShapeHash{0x8000ULL + op};
            entry.num_scalar_args = 2;
            entry.scalar_values.set(::fixy::index<0>(), first);
            entry.scalar_values.set(::fixy::index<1>(), second);
        });
        meta_starts.set(position, MetaIndex::none());
        ++count;
    }

    // An op with one output tensor and no metadata index, as the foreground
    // records it when the log is full.
    void push_op_without_meta(uint32_t op) {
        CRUCIBLE_TEST_ASSERT(kTest, count < CAPACITY);
        ::fixy::Index<CAPACITY> const position =
            ::fixy::Index<CAPACITY>::check(count).expect("a test reads an element at or past the count of its array");
        trace.update(position, [op](TraceRing::Entry& entry) noexcept {
            entry.schema_hash = crucible::SchemaHash{0x7000ULL + op};
            entry.shape_hash = crucible::ShapeHash{0x8000ULL + op};
            entry.num_outputs = 1;
        });
        meta_starts.set(position, MetaIndex::none());
        ++count;
    }

    // The closing entry of a nest whose op did not reach the Vigil, at the
    // depth of that op, with no tensor, as Vigil::give_unrecorded_nest gives it.
    void push_closing_entry(crucible::OpDepth depth) {
        CRUCIBLE_TEST_ASSERT(kTest, count < CAPACITY);
        ::fixy::Index<CAPACITY> const position =
            ::fixy::Index<CAPACITY>::check(count).expect("a test reads an element at or past the count of its array");
        trace.update(position, [](TraceRing::Entry& entry) noexcept {
            entry.schema_hash = crucible::kUnrecordedNestSchema;
            entry.shape_hash = crucible::ShapeHash{0x8100ULL};
        });
        meta_starts.set(position, MetaIndex::none());
        depths.set(position, depth);
        ++count;
    }
};

// The kernel classification of a build reads the global tables, which
// start() seals.  These tests build without start(), so they seal here.
void seal_global_tables() {
    crucible::global_schema_table().seal(kTest);
    crucible::global_ckernel_table().value()->seal(kTest);
}

// The bytes of the arena of a built region, and the bytes of the region store
// after the publish of that region.
struct PublishedBytes {
    Bytes built{size_t{0}};
    Bytes stored{size_t{0}};
};

// Makes a thread that runs no pipeline, with a ring of its own, the side of the
// log and the limits, and gives the value of the check of its log, the thread
// and its Stopped handle.
template <class Check>
auto with_idle_thread(crucible::test::RigLog& metalog, crucible::PipelineLimits const& limits, Check check) {
    crucible::test::RigRing ring{};
    return crucible::with_background_thread(
        ::foundation::effects::testing::test().alloc, ring.side(), metalog.side(), {.limits = limits},
        [&metalog, &check](BackgroundThread const& bt, auto stopped) noexcept { return check(metalog, bt, stopped); });
}

// Builds the region of the work from the log, and publishes it into the store
// of a new pipeline that runs no thread.  The store holds no other region, so
// its bytes are the bytes of the arena of the region after the publish.
[[nodiscard]] PublishedBytes publish_one_region(Work const& work, crucible::test::RigLog& metalog) {
    return with_idle_thread(
        metalog, crucible::PipelineLimits{},
        [&work](crucible::test::RigLog& log, BackgroundThread const& /*bt*/, auto& stopped) noexcept {
            const auto bg = ::foundation::effects::testing::bg();
            BuiltRegion region = work.build_region(stopped, *log.view());
            PublishedBytes bytes{.built = region.arena_bytes()};
            stopped.run_on_publish_stage([&](BackgroundThread::PublishStageView& owner,
                                             BackgroundThread::PublishStage const& stage) noexcept {
                owner.publish_region(bg, stage, std::move(region), StreamPoint{});
                CRUCIBLE_TEST_ASSERT(kTest, owner.region_count(stage) == 1u && "the store holds the published region");
                bytes.stored = owner.region_bytes(stage);
            });
            return bytes;
        });
}

}  // namespace
}  // namespace meta_rig

namespace test_background_thread_run_in_row {

// A build that meets an op with tensors and no metadata index makes no graph.
// It leaves the tail alone, because only the publish stage writes the tail.
template <class Stopped>
static void check_overflow_build_leaves_tail(crucible::test::RigLog& metalog, Stopped& stopped) noexcept {
    using namespace meta_rig;
    const auto test = ::foundation::effects::testing::test();
    // The bodies of the frames and of the epochs capture the log by a const
    // reference, and not the thread.
    MetaLog const& log = *metalog.view();
    crucible::NumericsHash const numerics = stopped.numerics();

    Work overflowing;
    (void)overflowing.push_op_with_meta(metalog, 0);
    (void)overflowing.push_op_with_meta(metalog, 1);
    overflowing.push_op_without_meta(2);

    crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
    const auto expect_overflow = [&log, numerics, &test, &scratch](BuildState& state, Work const& work) noexcept {
        scratch.arena.with_epoch(
            [&work, &state, &log, numerics, alloc = test.alloc](crucible::TraceScratch::Epoch& epoch) noexcept {
                const BackgroundThread::TraceBuild built = work.build(state, log, numerics, alloc, epoch);
                CRUCIBLE_TEST_ASSERT(kTest, built.err().expect("the overflow refuses the build")
                                                == BackgroundThread::BuildRefusal::MetaLogOverflow);
            });
    };
    stopped.with_trace_build_state(
        [&expect_overflow, &overflowing](BuildState& state) noexcept { expect_overflow(state, overflowing); });
    CRUCIBLE_TEST_ASSERT(kTest, metalog.total_released() == MetaIndex{0}
                                    && "the build stage moved the tail of the metadata log");

    // The overflow at the first op reads no metadata.
    Work overflowing_at_start;
    overflowing_at_start.push_op_without_meta(3);
    (void)overflowing_at_start.push_op_with_meta(metalog, 4);
    stopped.with_trace_build_state([&expect_overflow, &overflowing_at_start](BuildState& state) noexcept {
        expect_overflow(state, overflowing_at_start);
    });
    CRUCIBLE_TEST_ASSERT(kTest, metalog.total_released() == MetaIndex{0});
}

void test_overflow_build_leaves_tail() {
    meta_rig::seal_global_tables();
    crucible::test::RigLog metalog{};
    meta_rig::with_idle_thread(metalog, crucible::PipelineLimits{},
                               [](crucible::test::RigLog& log, BackgroundThread const& /*bt*/, auto& stopped) noexcept {
                                   check_overflow_build_leaves_tail(log, stopped);
                               });

    ::fixy::check::pass(kTest, "  overflow_build_leaves_tail:                PASSED\n");
}

// A work that holds the closing entry of a nest whose op did not reach the
// Vigil holds ops of a computation that the trace does not finish.  The build
// refuses it, so the region stays eager.  The same work with no closing entry
// builds.  The ops hold no tensor, so the build reads no metadata log.
template <class Stopped>
static void check_unrecorded_nest_refuses_the_build(crucible::test::RigLog& metalog, Stopped& stopped) noexcept {
    using namespace meta_rig;
    const auto test = ::foundation::effects::testing::test();
    crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
    // The build reads no record of this log.
    MetaLog const& log = *metalog.view();
    crucible::NumericsHash const numerics = stopped.numerics();

    Work raised;
    raised.push_op_with_scalars(0, 1, 2);
    raised.depths.set(::fixy::index<0>(), crucible::OpDepth{1});
    raised.push_closing_entry(crucible::OpDepth{0});
    raised.push_op_with_scalars(1, 3, 4);

    Work recorded;
    recorded.push_op_with_scalars(0, 1, 2);
    recorded.depths.set(::fixy::index<0>(), crucible::OpDepth{1});
    recorded.push_op_with_scalars(1, 3, 4);

    const auto build = [&raised, &recorded, &log, numerics, &test, &scratch](BuildState& state) noexcept {
        scratch.arena.with_epoch(
            [&raised, &state, &log, numerics, alloc = test.alloc](crucible::TraceScratch::Epoch& epoch) noexcept {
                const BackgroundThread::TraceBuild refused = raised.build(state, log, numerics, alloc, epoch);
                CRUCIBLE_TEST_ASSERT(kTest, refused.err().expect("the closing entry refuses the build")
                                                == BackgroundThread::BuildRefusal::UnrecordedNest);
            });
        scratch.arena.with_epoch(
            [&recorded, &state, &log, numerics, alloc = test.alloc](crucible::TraceScratch::Epoch& epoch) noexcept {
                CRUCIBLE_TEST_ASSERT(kTest, recorded.build(state, log, numerics, alloc, epoch).is_ok()
                                                && "the work with no closing entry builds");
            });
    };
    stopped.with_trace_build_state(build);
}

void test_unrecorded_nest_refuses_the_build() {
    meta_rig::seal_global_tables();
    crucible::test::RigLog metalog{};
    meta_rig::with_idle_thread(metalog, crucible::PipelineLimits{},
                               [](crucible::test::RigLog& log, BackgroundThread const& /*bt*/, auto& stopped) noexcept {
                                   check_unrecorded_nest_refuses_the_build(log, stopped);
                               });

    ::fixy::check::pass(kTest, "  unrecorded_nest_refuses_the_build:         PASSED\n");
}

// The order that the pipeline permits: the build stage builds a graph and
// then meets an overflow in the next work, before the publish stage publishes
// the graph.  The publish moves no tail.  Each release moves the tail to a
// floor past it, and a floor at or before the tail changes nothing.
template <class Stopped>
static void check_publish_stage_releases_in_order(crucible::test::RigLog& log, Stopped& stopped) noexcept {
    using namespace meta_rig;
    const auto test = ::foundation::effects::testing::test();
    const auto bg = ::foundation::effects::testing::bg();

    Work first;
    (void)first.push_op_with_meta(log, 0);
    (void)first.push_op_with_meta(log, 1);
    const uint64_t first_end = first.push_op_with_meta(log, 2);

    Work overflowing;
    (void)overflowing.push_op_with_meta(log, 3);
    const uint64_t overflow_end = overflowing.push_op_with_meta(log, 4);
    overflowing.push_op_without_meta(5);

    // The job takes the region of the build, which moves to the thread of the
    // publish stage, and not the build, which borrows the graph of the epoch.
    // No pipeline runs, so each job runs on this thread, and the caller reads
    // the tail after each one.
    crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
    MetaLog const& read_log = *log.view();
    crucible::NumericsHash const numerics = stopped.numerics();
    ::fixy::Option<BuiltRegion> first_built{};
    const auto build = [&first, &overflowing, &read_log, numerics, &test, &scratch,
                        &first_built](BuildState& state) noexcept {
        scratch.arena.with_epoch([&first, &state, &read_log, numerics, alloc = test.alloc,
                                  &first_built](crucible::TraceScratch::Epoch& epoch) noexcept {
            BackgroundThread::TraceBuild built = first.build(state, read_log, numerics, alloc, epoch);
            CRUCIBLE_TEST_ASSERT(kTest, built.is_ok());
            first_built = ::fixy::Option<BuiltRegion>::some(std::move(built).expect("the first work builds").region);
        });
        scratch.arena.with_epoch([&overflowing, &state, &read_log, numerics,
                                  alloc = test.alloc](crucible::TraceScratch::Epoch& epoch) noexcept {
            const BackgroundThread::TraceBuild overflowed = overflowing.build(state, read_log, numerics, alloc, epoch);
            CRUCIBLE_TEST_ASSERT(kTest, overflowed.is_err());
        });
    };
    stopped.with_trace_build_state(build);
    CRUCIBLE_TEST_ASSERT(kTest, log.total_released() == MetaIndex{0});

    BuiltRegion first_region = std::move(first_built).expect("the first epoch built the region");
    stopped.run_on_publish_stage(
        [&](BackgroundThread::PublishStageView& owner, BackgroundThread::PublishStage const& stage) noexcept {
            owner.publish_region(bg, stage, std::move(first_region), StreamPoint{});
        });
    CRUCIBLE_TEST_ASSERT(kTest, log.total_released() == MetaIndex{0} && "the publish of a region moved the tail");
    const auto release_to = [&stopped](MetaIndex meta_floor) {
        stopped.run_on_publish_stage([meta_floor](BackgroundThread::PublishStageView& owner,
                                                  BackgroundThread::PublishStage const& stage) noexcept {
            owner.release_meta_log(stage, meta_floor);
        });
    };
    release_to(MetaIndex{first_end});
    CRUCIBLE_TEST_ASSERT(kTest, log.total_released() == MetaIndex{first_end});
    release_to(MetaIndex{1});
    CRUCIBLE_TEST_ASSERT(kTest,
                         log.total_released() == MetaIndex{first_end} && "a floor before the tail moved the tail");
    release_to(MetaIndex{overflow_end});
    CRUCIBLE_TEST_ASSERT(kTest, log.total_released() == MetaIndex{overflow_end});
}

void test_publish_stage_releases_in_order() {
    meta_rig::seal_global_tables();
    crucible::test::RigLog metalog{};
    meta_rig::with_idle_thread(metalog, crucible::PipelineLimits{},
                               [](crucible::test::RigLog& log, BackgroundThread const& /*bt*/, auto& stopped) noexcept {
                                   check_publish_stage_releases_in_order(log, stopped);
                               });

    ::fixy::check::pass(kTest, "  publish_stage_releases_in_order:           PASSED\n");
}

// The release goes through the four stages of a running pipeline.  The last
// iteration that the detector cuts meets the overflow, and the work makes no
// region.  The floor of the last commit marker is the position of the K ops
// that the detect stage keeps, so the tail ends at the head: each record is
// free.
template <class Running>
static void check_pipeline_releases_after_overflow(crucible::test::TraceRig& rig, BackgroundThread const& bt,
                                                   Running running) noexcept {
    using namespace meta_rig;

    // The detector accepts the period of one iteration and then cuts each
    // iteration after it.  The last iteration below meets the overflow at
    // OVERFLOW_OP, after the ops before it put their metadata in the log.
    constexpr uint32_t OPS_PER_ITER = 8;
    constexpr uint32_t ITERS = 7;
    constexpr uint32_t OVERFLOW_OP = 4;
    auto push = [&](uint32_t op, MetaIndex index, uint16_t num_outputs) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{0x9000ULL + op};
        entry.shape_hash = crucible::ShapeHash{0xA000ULL + op};
        entry.num_outputs = num_outputs;
        while (!rig.append_entry(entry, index, crucible::ScopeHash{0}, crucible::CallsiteHash{0})) {
            CRUCIBLE_SPIN_PAUSE;
        }
    };

    uint64_t overflow_end = 0;
    for (uint32_t iter = 0; iter < ITERS; ++iter) {
        const bool is_last = iter + 1 == ITERS;
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op) {
            if (is_last && op == OVERFLOW_OP) {
                push(op, MetaIndex::none(), 1);
                continue;
            }
            const ::fixy::FixedArray<crucible::TensorMeta, 1> record{output_meta(op)};
            const MetaIndex index = rig.append_metas(record.view());
            CRUCIBLE_TEST_ASSERT(kTest, index.is_valid());
            if (is_last && op + 1 == OVERFLOW_OP) overflow_end = index.raw() + 1;
            push(op, index, 1);
        }
    }
    // The detector cuts an iteration when the first K ops of the next one
    // arrive.  These ops carry no tensor.
    for (uint32_t op = 0; op < crucible::IterationDetector::K; ++op)
        push(op, MetaIndex::none(), 0);

    const uint64_t target = rig.ring_total_produced();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest,
                         bt.total_processed.load_acquire() >= target && "the pipeline did not process every entry");
    static_cast<void>(std::move(running).stop());

    CRUCIBLE_TEST_ASSERT(kTest, overflow_end != 0u);
    CRUCIBLE_TEST_ASSERT(kTest, rig.log_total_released() == rig.log_total_produced()
                                    && "the floor of the last commit marker did not release each record");
}

void test_pipeline_releases_after_overflow() {
    crucible::test::TraceRig rig{};
    crucible::with_background_thread(
        ::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), {},
        [&rig](BackgroundThread const& bt, auto stopped) noexcept {
            check_pipeline_releases_after_overflow(
                rig, bt, std::move(stopped).start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}));
        });

    ::fixy::check::pass(kTest, "  pipeline_releases_after_overflow:          PASSED (tail={})\n",
                        static_cast<unsigned long long>(rig.log_total_released().raw()));
}

// The ops of the region of a built graph hold a copy of the run of the metadata
// log that the build read.  The floor of a later commit marker releases the
// run, and the foreground then writes new records over it.  The region lives as
// long as its memory, and no op points into the buffer of the log.
template <class Stopped>
static void check_built_graph_owns_its_metadata(crucible::test::RigLog& metalog, BackgroundThread const& bt,
                                                Stopped& stopped) noexcept {
    using namespace meta_rig;
    const auto test = ::foundation::effects::testing::test();
    const auto bg = ::foundation::effects::testing::bg();

    constexpr uint32_t OPS = 4;
    Work work;
    for (uint32_t op = 0; op < OPS; ++op)
        (void)work.push_op_with_meta(metalog, op);

    crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
    MetaLog const& log = *metalog.view();
    crucible::NumericsHash const numerics = stopped.numerics();
    ::fixy::Option<BuiltRegion> built_parts{};
    const auto build = [&work, &log, numerics, &test, &scratch, &built_parts](BuildState& state) noexcept {
        scratch.arena.with_epoch([&work, &state, &log, numerics, alloc = test.alloc,
                                  &built_parts](crucible::TraceScratch::Epoch& epoch) noexcept {
            auto [graph, region_parts] =
                work.build(state, log, numerics, alloc, epoch).expect("the work builds a graph");

            for (uint32_t op = 0; op < OPS; ++op) {
                // The expect ends the process when the op holds no output metadata.
                const crucible::TensorMeta& first_meta =
                    *graph->op(region_parts.ops(), crucible::OpIndex{op})
                         .output_metas()
                         .get(0)
                         .expect("a test reads an element at or past the count of its View");
                CRUCIBLE_TEST_ASSERT(kTest, !log.holds_record(first_meta)
                                                && "an op of the graph points into the buffer of the metadata log");
                CRUCIBLE_TEST_ASSERT(kTest, first_meta.data_ptr.value() == output_meta(op).data_ptr.value());
                CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_tensor_dim(first_meta.sizes[0]) == 64);
            }
            // The graph gives the entry of the ops that the caller gives, and no
            // copy of it.
            CRUCIBLE_TEST_ASSERT(kTest,
                                 ::fixy::address_of(::fixy::to(graph->op(region_parts.ops(), crucible::OpIndex{0})))
                                     == ::fixy::address_of(region_parts.ops().get(0).expect("the region holds op 0")));
            built_parts = ::fixy::Option<BuiltRegion>::some(std::move(region_parts));
        });
    };
    stopped.with_trace_build_state(build);
    BuiltRegion parts = std::move(built_parts).expect("the epoch built the region");
    ::fixy::Address const first_op_address =
        ::fixy::address_of(parts.ops().get(0).expect("the region of the build holds op 0"));

    // The region of the publish holds the same ops, and a release to the end
    // of the run moves the tail past it.
    stopped.run_on_publish_stage(
        [&](BackgroundThread::PublishStageView& owner, BackgroundThread::PublishStage const& stage) noexcept {
            owner.publish_region(bg, stage, std::move(parts), StreamPoint{});
            owner.release_meta_log(stage, MetaIndex{OPS});
        });
    const crucible::DagRef<const crucible::RegionNode> region =
        bt.active_region().expect("the publish gives the active region");
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::address_of(::fixy::to(region->op_at(0))) == first_op_address);
    CRUCIBLE_TEST_ASSERT(kTest, metalog.total_released() == MetaIndex{OPS});
}

void test_built_graph_owns_its_metadata() {
    meta_rig::seal_global_tables();
    crucible::test::RigLog metalog{};
    meta_rig::with_idle_thread(metalog, crucible::PipelineLimits{},
                               [](crucible::test::RigLog& log, BackgroundThread const& bt, auto& stopped) noexcept {
                                   check_built_graph_owns_its_metadata(log, bt, stopped);
                               });

    ::fixy::check::pass(kTest, "  built_graph_owns_its_metadata:             PASSED\n");
}

// The log starts two records before the wrap of 32 bits.
static constexpr uint32_t BELOW_WRAP = 0xFFFFFFFEu;

// The two counters of the metadata log count each record that the log held.
// A run of ops whose metadata passes 2^32 records builds, the region holds the
// metadata of each op, and the release moves the tail past 2^32.
template <class Stopped>
static void check_build_and_release_pass_two_to_the_32(crucible::test::RigLog& metalog, Stopped& stopped) noexcept {
    using namespace meta_rig;
    const auto test = ::foundation::effects::testing::test();
    const auto bg = ::foundation::effects::testing::bg();

    constexpr uint32_t OPS = 4;
    Work work;
    uint64_t run_end = 0;
    for (uint32_t op = 0; op < OPS; ++op)
        run_end = work.push_op_with_meta(metalog, op);
    CRUCIBLE_TEST_ASSERT(kTest, run_end == uint64_t{BELOW_WRAP} + OPS);

    crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
    MetaLog const& log = *metalog.view();
    crucible::NumericsHash const numerics = stopped.numerics();
    ::fixy::Option<BuiltRegion> built_parts{};
    const auto build = [&work, &log, numerics, &test, &scratch, &built_parts](BuildState& state) noexcept {
        scratch.arena.with_epoch([&work, &state, &log, numerics, alloc = test.alloc,
                                  &built_parts](crucible::TraceScratch::Epoch& epoch) noexcept {
            auto [graph, region_parts] =
                work.build(state, log, numerics, alloc, epoch).expect("the work builds a graph");
            for (uint32_t op = 0; op < OPS; ++op) {
                const crucible::TensorMeta& first_meta =
                    *graph->op(region_parts.ops(), crucible::OpIndex{op})
                         .output_metas()
                         .get(0)
                         .expect("a test reads an element at or past the count of its View");
                CRUCIBLE_TEST_ASSERT(kTest, first_meta.data_ptr.value() == output_meta(op).data_ptr.value());
            }
            built_parts = ::fixy::Option<BuiltRegion>::some(std::move(region_parts));
        });
    };
    stopped.with_trace_build_state(build);
    BuiltRegion parts = std::move(built_parts).expect("the epoch built the region");

    stopped.run_on_publish_stage(
        [&](BackgroundThread::PublishStageView& owner, BackgroundThread::PublishStage const& stage) noexcept {
            owner.publish_region(bg, stage, std::move(parts), StreamPoint{});
            owner.release_meta_log(stage, MetaIndex{run_end});
        });
    CRUCIBLE_TEST_ASSERT(kTest, metalog.total_released() == MetaIndex{run_end}
                                    && "the release did not move the tail past 2^32");
}

void test_build_and_release_pass_two_to_the_32() {
    meta_rig::seal_global_tables();
    crucible::test::RigLog metalog{MetaIndex{BELOW_WRAP}};
    meta_rig::with_idle_thread(metalog, crucible::PipelineLimits{},
                               [](crucible::test::RigLog& log, BackgroundThread const& /*bt*/, auto& stopped) noexcept {
                                   check_build_and_release_pass_two_to_the_32(log, stopped);
                               });

    ::fixy::check::pass(kTest, "  build_and_release_pass_two_to_the_32:      PASSED\n");
}

// An op with no tensors has no metadata index.  Its scalar arguments still go
// into its entry, so they fold into the content hash.  Two such ops that
// differ only in a scalar then give two content hashes.  Before, the entry
// kept the count and no scalar, and the two hashes were one.
template <class Stopped>
static void check_scalars_of_an_op_with_no_tensors(crucible::test::RigLog& metalog, Stopped& stopped) noexcept {
    using namespace meta_rig;
    // The ops have no metadata index, so the build reads no record of the log.
    const auto test = ::foundation::effects::testing::test();
    crucible::TraceScratch scratch{Bytes{size_t{1} << 16}};
    MetaLog const& log = *metalog.view();
    crucible::NumericsHash const numerics = stopped.numerics();

    Work five_six;
    five_six.push_op_with_scalars(0, 5, 6);
    Work five_seven;
    five_seven.push_op_with_scalars(0, 5, 7);
    ::fixy::Option<BuiltRegion> built_a{};
    ::fixy::Option<BuiltRegion> built_b{};
    const auto build = [&five_six, &five_seven, &log, numerics, &test, &scratch, &built_a,
                        &built_b](BuildState& state) noexcept {
        scratch.arena.with_epoch([&five_six, &five_seven, &state, &log, numerics, alloc = test.alloc, &built_a,
                                  &built_b](crucible::TraceScratch::Epoch& epoch) noexcept {
            built_a = ::fixy::Option<BuiltRegion>::some(
                five_six.build(state, log, numerics, alloc, epoch).expect("the work builds a graph").region);
            built_b = ::fixy::Option<BuiltRegion>::some(
                five_seven.build(state, log, numerics, alloc, epoch).expect("the work builds a graph").region);
        });
    };
    stopped.with_trace_build_state(build);
    BuiltRegion const region_a = std::move(built_a).expect("the epoch built the first region");
    BuiltRegion const region_b = std::move(built_b).expect("the epoch built the second region");

    const crucible::TraceEntry& op = *region_a.ops().get(0).expect("the region holds its one op");
    CRUCIBLE_TEST_ASSERT(kTest, op.num_inputs() == 0 && op.num_outputs() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, op.num_scalar_args() == 2 && op.scalar_args().size() == 2);
    CRUCIBLE_TEST_ASSERT(
        kTest, *op.scalar_args().get(0).expect("a test reads an element at or past the count of its View") == 5);
    CRUCIBLE_TEST_ASSERT(
        kTest, *op.scalar_args().get(1).expect("a test reads an element at or past the count of its View") == 6);
    CRUCIBLE_TEST_ASSERT(kTest, region_a.content_hash() != region_b.content_hash()
                                    && "two ops that differ only in a scalar must give two content hashes");
}

void test_scalars_of_an_op_with_no_tensors() {
    meta_rig::seal_global_tables();
    crucible::test::RigLog metalog{};
    meta_rig::with_idle_thread(metalog, crucible::PipelineLimits{},
                               [](crucible::test::RigLog& log, BackgroundThread const& /*bt*/, auto& stopped) noexcept {
                                   check_scalars_of_an_op_with_no_tensors(log, stopped);
                               });

    ::fixy::check::pass(kTest, "  scalars_of_an_op_with_no_tensors:          PASSED\n");
}

// The publish of a region builds its RegionNode in the arena of the region,
// and its MemoryPlan there too when the region has slots.  The build sizes
// the first block of that arena for the runs, the node and the plan, so the
// publish takes no block: the bytes of the arena after the publish are its
// bytes after the build.  A second block holds at least twice the bytes of
// the first, so a publish that takes one at least triples the bytes.
//
// Each output of the first work has a data pointer of its own, so each one is
// a slot of its own, and the run of the slots holds no spare slot.  The
// second work has no tensor, thus no slot, and its region has a node and no
// plan.
void test_publish_adds_no_block_to_the_region_arena() {
    using namespace meta_rig;
    seal_global_tables();

    crucible::test::RigLog metalog{};
    constexpr uint32_t OPS = 4;
    Work slotted;
    for (uint32_t op = 0; op < OPS; ++op)
        (void)slotted.push_op_with_meta(metalog, op);
    Work unslotted;
    unslotted.push_op_with_scalars(0, 5, 6);

    const PublishedBytes with_plan = publish_one_region(slotted, metalog);
    const PublishedBytes with_no_plan = publish_one_region(unslotted, metalog);
    CRUCIBLE_TEST_ASSERT(kTest, with_plan.built > Bytes{size_t{0}} && with_no_plan.built > Bytes{size_t{0}});
    CRUCIBLE_TEST_ASSERT(kTest,
                         with_plan.stored == with_plan.built && "the publish of a region with slots took a block");
    CRUCIBLE_TEST_ASSERT(kTest, with_no_plan.stored == with_no_plan.built
                                    && "the publish of a region with no slot took a block");

    ::fixy::check::pass(kTest, "  publish_adds_no_block_to_the_region_arena: PASSED\n");
}

// The bytes of the publish scratch, which a job reads through either handle.
template <class Handle>
static Bytes publish_scratch_bytes(Handle& handle) noexcept {
    Bytes bytes{size_t{0}};
    handle.run_on_publish_stage(
        [&bytes](BackgroundThread::PublishStageView& owner, BackgroundThread::PublishStage const& stage) noexcept {
            bytes = owner.publish_scratch_retained_bytes(stage);
        });
    return bytes;
}

// While the pipeline runs, a job runs on the publish stage, between two
// publications.  The publish stage writes the publish scratch at each publish,
// and the thread of the Running handle reads the bytes of that scratch in a
// job.  The mailbox of the jobs orders the two, so the thread sanitizer reports
// no race.  Each region has a slot, so its publish computes a plan in the
// scratch.  The bytes of the build scratch have no door of a Running handle.
// The Stopped handle of the stop reads them.
template <class Running>
static void check_scratch_bytes_beside_a_publish(crucible::test::TraceRig& rig, BackgroundThread const& bt,
                                                 Running running) noexcept {
    using namespace meta_rig;
    constexpr uint32_t OPS_PER_ITER = 8;
    constexpr uint32_t ITERS = 32;
    Bytes last = publish_scratch_bytes(running);
    for (uint32_t iter = 0; iter < ITERS; ++iter) {
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op) {
            const ::fixy::FixedArray<crucible::TensorMeta, 1> record{output_meta(op)};
            const MetaIndex index = rig.append_metas(record.view());
            CRUCIBLE_TEST_ASSERT(kTest, index.is_valid());
            TraceRing::Entry entry{};
            entry.schema_hash = crucible::SchemaHash{0x9000ULL + op};
            entry.shape_hash = crucible::ShapeHash{0xA000ULL + op};
            entry.num_outputs = 1;
            while (!rig.append_entry(entry, index, crucible::ScopeHash{0}, crucible::CallsiteHash{0})) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        const Bytes bytes = publish_scratch_bytes(running);
        CRUCIBLE_TEST_ASSERT(kTest, bytes >= last && "the publish scratch keeps its first block");
        last = bytes;
    }
    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest, bt.uncompiled_regions().total() > 0u && "the pipeline published no region");
    auto stopped = std::move(running).stop();
    CRUCIBLE_TEST_ASSERT(kTest, publish_scratch_bytes(stopped) > Bytes{size_t{0}} && "no publish used the scratch");
    CRUCIBLE_TEST_ASSERT(kTest,
                         stopped.build_scratch_retained_bytes() > Bytes{size_t{0}} && "no build used the scratch");
}

void test_scratch_bytes_beside_a_publish() {
    crucible::test::TraceRig rig{};
    crucible::with_background_thread(
        ::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), {},
        [&rig](BackgroundThread const& bt, auto stopped) noexcept {
            check_scratch_bytes_beside_a_publish(
                rig, bt, std::move(stopped).start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}));
        });

    ::fixy::check::pass(kTest, "  scratch_bytes_beside_a_publish:            PASSED\n");
}

// With no foreground, the store keeps the regions of the uncompiled queue and
// the last published region, and no other region.  A region that left the
// queue leaves the store at the sweep of that publish.  Each publish runs in a
// job, so the test counts the store after each one.  It also reads the content
// hash of each region of the queue, so a region that the sweep released too
// early is a use after free under the address sanitizer.
template <class Stopped>
static void check_store_releases_what_left_the_queue(crucible::test::RigLog& metalog, BackgroundThread const& bt,
                                                     Stopped& stopped) noexcept {
    using namespace meta_rig;
    const auto bg = ::foundation::effects::testing::bg();
    const uint32_t capacity = bt.uncompiled_regions().capacity();
    constexpr uint32_t PUBLISHES = 20;
    ::fixy::FixedArray<crucible::ContentHash, PUBLISHES> hashes{};
    for (const ::fixy::Index<PUBLISHES> publish : hashes.indices()) {
        Work work;
        work.push_op_with_scalars(0, static_cast<int64_t>(publish.raw()), 7);
        BuiltRegion region = work.build_region(stopped, *metalog.view());
        hashes.set(publish, region.content_hash());
        const auto publish_and_check = [&](BackgroundThread::PublishStageView& owner,
                                           BackgroundThread::PublishStage const& stage) noexcept {
            owner.publish_region(bg, stage, std::move(region), StreamPoint{});
            const uint32_t published = static_cast<uint32_t>(publish.raw()) + 1u;
            const uint32_t queued = ::fixy::min(published, capacity);
            CRUCIBLE_TEST_ASSERT(kTest, owner.region_count(stage) == ::fixy::max(queued, 1u)
                                            && "the store keeps the queue and the last region only");
            CRUCIBLE_TEST_ASSERT(kTest, owner.uncompiled_regions().size() == queued);
            for (uint32_t age = 0; age < queued; ++age) {
                const ::fixy::Index<PUBLISHES> pushed = ::fixy::Index<PUBLISHES>::check(publish.raw() - age)
                                                            .expect("the queue holds at most the published regions");
                const crucible::DagRef<crucible::RegionNode> node =
                    owner.uncompiled_regions().at(stage, age).expect("a slot below the size holds a region");
                CRUCIBLE_TEST_ASSERT(kTest, node->content_hash == hashes.at(pushed));
            }
            CRUCIBLE_TEST_ASSERT(kTest, owner.regions_dropped_full(stage) == 0u);
        };
        stopped.run_on_publish_stage(publish_and_check);
    }
}

void test_store_releases_what_left_the_queue() {
    using Count = ::fixy::Quantity<::fixy::unit::Element, uint32_t>;
    meta_rig::seal_global_tables();
    const ::fixy::FixedArray<uint32_t, 3> capacities{0u, 1u, 3u};
    for (const uint32_t capacity : capacities) {
        crucible::test::RigLog metalog{};
        crucible::PipelineLimits limits{};
        limits.region_store = Count{8};
        limits.uncompiled_queue = Count{capacity};
        meta_rig::with_idle_thread(metalog, limits,
                                   [](crucible::test::RigLog& log, BackgroundThread const& bt, auto& stopped) noexcept {
                                       check_store_releases_what_left_the_queue(log, bt, stopped);
                                   });
    }

    ::fixy::check::pass(kTest, "  store_releases_what_left_the_queue:        PASSED\n");
}

}  // namespace test_background_thread_run_in_row
