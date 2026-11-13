// The release of the metadata log in test_background_thread_run_in_row, and
// the copy of its records that a built graph holds.
//
// The foreground appends the tensor metadata of each op to the metadata log.
// Each commit marker of the pipeline carries a release floor: the oldest
// record that a later work can hold.  The publish stage moves the tail to the
// floor of each marker, and it is the one thread that writes the tail.  The
// publish of a region and a build that meets an overflow move no tail.

#include "background_thread_run_in_row.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CKernel.h>
#include <crucible/IterationDetector.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <foundation/effects/Effect.h>
#include "test_assert.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::MetaLog;
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

// One work item for build_trace_from: the parallel arrays of a run of ops.
struct Work {
    static constexpr uint32_t CAPACITY = 8;
    uint32_t count = 0;
    ::fixy::FixedArray<TraceRing::Entry, CAPACITY> trace{};
    ::fixy::FixedArray<MetaIndex, CAPACITY> meta_starts{};
    ::fixy::FixedArray<crucible::ScopeHash, CAPACITY> scope_hashes{};
    ::fixy::FixedArray<crucible::CallsiteHash, CAPACITY> callsite_hashes{};

    // The graph of the count ops of the work.  The build reads the first
    // count elements of each array.
    [[nodiscard]] BackgroundThread::TraceBuild build(BackgroundThread& bt, ::foundation::effects::Alloc alloc,
                                                     ::fixy::BumpArena& scratch) const {
        return bt.build_trace_from(alloc, scratch, first_ops(trace), first_ops(meta_starts), first_ops(scope_hashes),
                                   first_ops(callsite_hashes));
    }

    // The first count elements of one array of the work.
    template <class T>
    [[nodiscard]] ::fixy::View<const T> first_ops(::fixy::FixedArray<T, CAPACITY> const& column) const {
        return column.view().window(0, count).expect("a work holds at most CAPACITY ops");
    }

    // An op with one output tensor, whose metadata goes to the log.  Returns
    // the end of that metadata in the log.
    uint64_t push_op_with_meta(MetaLog& log, uint32_t op) {
        assert(count < CAPACITY);
        const crucible::TensorMeta meta = output_meta(op);
        const MetaIndex index = log.try_append(&meta, 1);
        assert(index.is_valid());
        trace[count].schema_hash = crucible::SchemaHash{0x7000ULL + op};
        trace[count].shape_hash = crucible::ShapeHash{0x8000ULL + op};
        trace[count].num_outputs = 1;
        meta_starts[count] = index;
        ++count;
        return index.raw() + 1;
    }

    // An op with one output tensor and no metadata index, as the foreground
    // records it when the log is full.
    void push_op_without_meta(uint32_t op) {
        assert(count < CAPACITY);
        trace[count].schema_hash = crucible::SchemaHash{0x7000ULL + op};
        trace[count].shape_hash = crucible::ShapeHash{0x8000ULL + op};
        trace[count].num_outputs = 1;
        meta_starts[count] = MetaIndex::none();
        ++count;
    }
};

// The kernel classification of a build reads the global tables, which
// start() seals.  These tests build without start(), so they seal here.
void seal_global_tables() {
    crucible::global_schema_table().seal();
    crucible::global_ckernel_table().value()->seal();
}

}  // namespace
}  // namespace meta_rig

namespace test_background_thread_run_in_row {

// A build that meets an op with tensors and no metadata index makes no graph.
// It leaves the tail alone, because only the publish stage writes the tail.
void test_overflow_build_leaves_tail() {
    using namespace meta_rig;
    seal_global_tables();

    BackgroundThread bt;
    auto metalog = std::make_unique<MetaLog>();
    bt.meta_log.set(metalog.get());
    const auto test = ::foundation::effects::testing::test();

    Work overflowing;
    (void)overflowing.push_op_with_meta(*metalog, 0);
    (void)overflowing.push_op_with_meta(*metalog, 1);
    overflowing.push_op_without_meta(2);

    ::fixy::BumpArena scratch{size_t{1} << 16};
    const BackgroundThread::TraceBuild built = overflowing.build(bt, test.alloc, scratch);
    assert(built.is_err());
    assert(metalog->tail.get() == 0u && "the build stage moved the tail of the metadata log");

    // The overflow at the first op reads no metadata.
    Work overflowing_at_start;
    overflowing_at_start.push_op_without_meta(3);
    (void)overflowing_at_start.push_op_with_meta(*metalog, 4);
    const BackgroundThread::TraceBuild built_at_start = overflowing_at_start.build(bt, test.alloc, scratch);
    assert(built_at_start.is_err());
    assert(metalog->tail.get() == 0u);

    crucible::test::pass("  overflow_build_leaves_tail:                PASSED\n");
}

// The order that the pipeline permits: the build stage builds a graph and
// then meets an overflow in the next work, before the publish stage publishes
// the graph.  The publish moves no tail.  Each release moves the tail to a
// floor past it, and a floor at or before the tail changes nothing.
void test_publish_stage_releases_in_order() {
    using namespace meta_rig;
    seal_global_tables();

    BackgroundThread bt;
    auto metalog = std::make_unique<MetaLog>();
    bt.meta_log.set(metalog.get());
    const auto test = ::foundation::effects::testing::test();
    const auto bg = ::foundation::effects::testing::bg();

    Work first;
    (void)first.push_op_with_meta(*metalog, 0);
    (void)first.push_op_with_meta(*metalog, 1);
    const uint64_t first_end = first.push_op_with_meta(*metalog, 2);

    Work overflowing;
    (void)overflowing.push_op_with_meta(*metalog, 3);
    const uint64_t overflow_end = overflowing.push_op_with_meta(*metalog, 4);
    overflowing.push_op_without_meta(5);

    ::fixy::BumpArena scratch{size_t{1} << 16};
    BackgroundThread::TraceBuild built = first.build(bt, test.alloc, scratch);
    assert(built.is_ok());
    const BackgroundThread::TraceBuild overflowed = overflowing.build(bt, test.alloc, scratch);
    assert(overflowed.is_err());
    assert(metalog->tail.get() == 0u);

    bt.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        bt.publish_region(bg, stage, std::move(built).expect("the first work builds").region,
                          BackgroundThread::StreamPoint{});
        assert(bt.meta_log.get()->tail.get() == 0u && "the publish of a region moved the tail");
        bt.release_meta_log(stage, first_end);
        assert(bt.meta_log.get()->tail.get() == first_end);
        bt.release_meta_log(stage, 1);
        assert(bt.meta_log.get()->tail.get() == first_end && "a floor before the tail moved the tail");
        bt.release_meta_log(stage, overflow_end);
    });
    assert(metalog->tail.get() == overflow_end);

    crucible::test::pass("  publish_stage_releases_in_order:           PASSED\n");
}

// The release goes through the four stages of a running pipeline.  The last
// iteration that the detector cuts meets the overflow, and the work makes no
// region.  The floor of the last commit marker is the position of the K ops
// that the detect stage keeps, so the tail ends at the head: each record is
// free.
void test_pipeline_releases_after_overflow() {
    using namespace meta_rig;

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.start(ring.get(), metalog.get());

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
        while (!ring->try_append_pinned(entry, index, crucible::ScopeHash{0}, crucible::CallsiteHash{0}).peek()) {
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
            const crucible::TensorMeta meta = output_meta(op);
            const MetaIndex index = metalog->try_append(&meta, 1);
            assert(index.is_valid());
            if (is_last && op + 1 == OVERFLOW_OP) overflow_end = index.raw() + 1;
            push(op, index, 1);
        }
    }
    // The detector cuts an iteration when the first K ops of the next one
    // arrive.  These ops carry no tensor.
    for (uint32_t op = 0; op < crucible::IterationDetector::K; ++op)
        push(op, MetaIndex::none(), 0);

    const uint64_t target = ring->total_produced();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.total_processed.load_acquire() >= target && "the pipeline did not process every entry");
    bt.stop();

    assert(overflow_end != 0u);
    assert(bt.meta_log.get()->tail.get() == bt.meta_log.get()->head.get()
           && "the floor of the last commit marker did not release each record");

    crucible::test::pass("  pipeline_releases_after_overflow:          PASSED (tail={})\n",
                         static_cast<unsigned long long>(metalog->tail.get()));
}

// The ops of a built graph hold a copy of the run of the metadata log that
// the build read.  The floor of a later commit marker releases the run, and
// the foreground then writes new records over it.  The region lives as long
// as its memory, and no op points into the buffer of the log.
void test_built_graph_owns_its_metadata() {
    using namespace meta_rig;
    seal_global_tables();

    BackgroundThread bt;
    auto metalog = std::make_unique<MetaLog>();
    bt.meta_log.set(metalog.get());
    const auto test = ::foundation::effects::testing::test();
    const auto bg = ::foundation::effects::testing::bg();

    constexpr uint32_t OPS = 4;
    Work work;
    for (uint32_t op = 0; op < OPS; ++op)
        (void)work.push_op_with_meta(*metalog, op);

    ::fixy::BumpArena scratch{size_t{1} << 16};
    BackgroundThread::TraceBuild built = work.build(bt, test.alloc, scratch);
    auto [graph, parts] = std::move(built).expect("the work builds a graph");

    const std::less<const crucible::TensorMeta*> before{};
    const crucible::TensorMeta* const log_begin = metalog->entries_buffer_.data();
    const crucible::TensorMeta* const log_end = log_begin + MetaLog::CAPACITY;
    for (uint32_t op = 0; op < OPS; ++op) {
        // element_at ends the process when the op holds no output metadata.
        const crucible::TensorMeta& first_meta = crucible::test::element_at(graph->ops[op].output_metas(), 0);
        const crucible::TensorMeta* const metas = &first_meta;
        assert((before(metas, log_begin) || !before(metas, log_end))
               && "an op of the graph points into the buffer of the metadata log");
        assert(crucible::raw_data_ptr(first_meta) == crucible::raw_data_ptr(output_meta(op)));
        assert(crucible::raw_tensor_dim(first_meta.sizes[0]) == 64);
    }

    // The region of the publish holds the same ops, and a release to the end
    // of the run moves the tail past it.
    bt.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        bt.publish_region(bg, stage, std::move(parts), BackgroundThread::StreamPoint{});
        bt.release_meta_log(stage, OPS);
    });
    const crucible::RegionNode* const region = bt.active_region.load_acquire().try_get();
    assert(region != nullptr && &region->op_at(0) == graph->ops);
    assert(metalog->tail.get() == OPS);

    crucible::test::pass("  built_graph_owns_its_metadata:             PASSED\n");
}

// The two counters of the metadata log count each record that the log held.
// A run of ops whose metadata passes 2^32 records builds, the graph holds the
// metadata of each op, and the release moves the tail past 2^32.
void test_build_and_release_pass_two_to_the_32() {
    using namespace meta_rig;
    seal_global_tables();

    BackgroundThread bt;
    auto metalog = std::make_unique<MetaLog>();
    bt.meta_log.set(metalog.get());
    const auto test = ::foundation::effects::testing::test();
    const auto bg = ::foundation::effects::testing::bg();

    // No other thread uses the log, which is the condition of the reset.
    constexpr uint32_t BELOW_WRAP = 0xFFFFFFFEu;
    metalog->head.reset_under_quiescence(BELOW_WRAP);
    metalog->tail.reset_under_quiescence(BELOW_WRAP);
    metalog->cached_tail_.reset_under_quiescence(BELOW_WRAP);

    constexpr uint32_t OPS = 4;
    Work work;
    uint64_t run_end = 0;
    for (uint32_t op = 0; op < OPS; ++op)
        run_end = work.push_op_with_meta(*metalog, op);
    assert(run_end == uint64_t{BELOW_WRAP} + OPS);

    ::fixy::BumpArena scratch{size_t{1} << 16};
    BackgroundThread::TraceBuild built = work.build(bt, test.alloc, scratch);
    auto [graph, parts] = std::move(built).expect("the work builds a graph");
    for (uint32_t op = 0; op < OPS; ++op) {
        const crucible::TensorMeta& first_meta = crucible::test::element_at(graph->ops[op].output_metas(), 0);
        assert(crucible::raw_data_ptr(first_meta) == crucible::raw_data_ptr(output_meta(op)));
    }

    bt.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        bt.publish_region(bg, stage, std::move(parts), BackgroundThread::StreamPoint{});
        bt.release_meta_log(stage, run_end);
    });
    assert(metalog->tail.get() == run_end && "the release did not move the tail past 2^32");

    crucible::test::pass("  build_and_release_pass_two_to_the_32:      PASSED\n");
}

}  // namespace test_background_thread_run_in_row
