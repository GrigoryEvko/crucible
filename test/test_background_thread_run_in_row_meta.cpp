// The release of the metadata log in test_background_thread_run_in_row, and
// the copy of its records that a built graph holds.
//
// The foreground appends the tensor metadata of each op to the metadata log.
// The pipeline releases the metadata of an iteration when it is done with it,
// and the release moves the tail of the log.  The tail only moves forward, and
// one thread writes it: the publish stage.  The works reach the publish stage
// in the order of the recording, so each release is past the one before.
//
// A build that meets an op with tensors and no metadata index makes no graph.
// The metadata that it read before that op is dead, and the release of it goes
// to the publish stage too.  The build stage runs ahead of the publish stage,
// so a release from the build stage passes the end of a graph that is still
// on its way to the publish stage.  The publish of that graph then moves the
// tail back, and the contract of the tail stops the process.

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
    TraceRing::Entry trace[CAPACITY]{};
    MetaIndex meta_starts[CAPACITY]{};
    crucible::ScopeHash scope_hashes[CAPACITY]{};
    crucible::CallsiteHash callsite_hashes[CAPACITY]{};

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
// It reports the end of the metadata that it read, and it leaves the tail
// alone, because only the publish stage writes the tail.
void test_overflow_build_leaves_tail() {
    using namespace meta_rig;
    seal_global_tables();

    BackgroundThread bt;
    auto metalog = std::make_unique<MetaLog>();
    bt.meta_log.set(metalog.get());
    const auto test = ::foundation::effects::testing::test();

    Work overflowing;
    (void)overflowing.push_op_with_meta(*metalog, 0);
    const uint64_t read_end = overflowing.push_op_with_meta(*metalog, 1);
    overflowing.push_op_without_meta(2);

    const BackgroundThread::TraceBuild built =
        bt.build_trace_from(test.alloc, overflowing.count, overflowing.trace, overflowing.meta_starts,
                            overflowing.scope_hashes, overflowing.callsite_hashes);
    assert(!built.has_value());
    assert(built.error().meta_end == read_end);
    assert(metalog->tail.get() == 0u && "the build stage moved the tail of the metadata log");

    // The overflow at the first op reads no metadata, so the release is
    // empty.
    Work overflowing_at_start;
    overflowing_at_start.push_op_without_meta(3);
    (void)overflowing_at_start.push_op_with_meta(*metalog, 4);
    const BackgroundThread::TraceBuild built_at_start = bt.build_trace_from(
        test.alloc, overflowing_at_start.count, overflowing_at_start.trace, overflowing_at_start.meta_starts,
        overflowing_at_start.scope_hashes, overflowing_at_start.callsite_hashes);
    assert(!built_at_start.has_value());
    assert(built_at_start.error().meta_end == 0u);
    assert(metalog->tail.get() == 0u);

    crucible::test::pass("  overflow_build_leaves_tail:                PASSED\n");
}

// The order that the pipeline permits: the build stage builds a graph and
// then meets an overflow in the next work, before the publish stage publishes
// the graph.  The publish stage then applies the release of each work in the
// order of the works, and the tail only moves forward.
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

    const BackgroundThread::TraceBuild built = bt.build_trace_from(
        test.alloc, first.count, first.trace, first.meta_starts, first.scope_hashes, first.callsite_hashes);
    assert(built.has_value() && *built != nullptr);
    const BackgroundThread::TraceBuild overflowed =
        bt.build_trace_from(test.alloc, overflowing.count, overflowing.trace, overflowing.meta_starts,
                            overflowing.scope_hashes, overflowing.callsite_hashes);
    assert(!overflowed.has_value());
    assert(metalog->tail.get() == 0u);

    bt.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        bt.publish_trace_graph(bg, stage, *built);
        assert(metalog->tail.get() == first_end);
        bt.release_meta_log(stage, overflowed.error().meta_end);
    });
    assert(metalog->tail.get() == overflow_end);

    crucible::test::pass("  publish_stage_releases_in_order:           PASSED\n");
}

// The release goes through the four stages of a running pipeline.  The last
// iteration that the detector cuts meets the overflow, so its release is the
// last move of the tail, and the tail ends at the metadata that it read.
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
    assert(metalog->tail.get() == overflow_end && "the publish stage did not apply the release of the last iteration");

    crucible::test::pass("  pipeline_releases_after_overflow:          PASSED (tail={})\n",
                         static_cast<unsigned long long>(metalog->tail.get()));
}

// The ops of a built graph hold a copy of the run of the metadata log that
// the build read.  The publish stage releases the run when it publishes the
// region.  The foreground then writes new records over the run.  The region
// lives as long as the arena, and no op points into the buffer of the log.
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

    const BackgroundThread::TraceBuild built =
        bt.build_trace_from(test.alloc, work.count, work.trace, work.meta_starts, work.scope_hashes,
                            work.callsite_hashes);
    assert(built.has_value() && *built != nullptr);

    const std::less<const crucible::TensorMeta*> before{};
    const crucible::TensorMeta* const log_begin = metalog->entries;
    const crucible::TensorMeta* const log_end = log_begin + MetaLog::CAPACITY;
    for (uint32_t op = 0; op < OPS; ++op) {
        const crucible::TensorMeta* const metas = (*built)->ops[op].output_metas;
        assert(metas != nullptr);
        assert((before(metas, log_begin) || !before(metas, log_end))
               && "an op of the graph points into the buffer of the metadata log");
        assert(crucible::raw_data_ptr(metas[0]) == crucible::raw_data_ptr(output_meta(op)));
        assert(crucible::raw_tensor_dim(metas[0].sizes[0]) == 64);
    }

    // The region of the publish holds the same ops, and the release moves
    // the tail past the run.
    bt.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        bt.publish_trace_graph(bg, stage, *built);
    });
    const crucible::RegionNode* const region = bt.active_region.load(std::memory_order_acquire);
    assert(region != nullptr && region->ops == (*built)->ops);
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
    constexpr uint32_t BELOW_WRAP = 0xFFFF'FFFEu;
    metalog->head.reset_under_quiescence(BELOW_WRAP);
    metalog->tail.reset_under_quiescence(BELOW_WRAP);
    metalog->cached_tail_.reset_under_quiescence(BELOW_WRAP);

    constexpr uint32_t OPS = 4;
    Work work;
    uint64_t run_end = 0;
    for (uint32_t op = 0; op < OPS; ++op)
        run_end = work.push_op_with_meta(*metalog, op);
    assert(run_end == uint64_t{BELOW_WRAP} + OPS);

    const BackgroundThread::TraceBuild built =
        bt.build_trace_from(test.alloc, work.count, work.trace, work.meta_starts, work.scope_hashes,
                            work.callsite_hashes);
    assert(built.has_value() && *built != nullptr);
    for (uint32_t op = 0; op < OPS; ++op) {
        const crucible::TensorMeta* const metas = (*built)->ops[op].output_metas;
        assert(metas != nullptr);
        assert(crucible::raw_data_ptr(metas[0]) == crucible::raw_data_ptr(output_meta(op)));
    }

    bt.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        bt.publish_trace_graph(bg, stage, *built);
    });
    assert(metalog->tail.get() == run_end && "the release did not move the tail past 2^32");

    crucible::test::pass("  build_and_release_pass_two_to_the_32:      PASSED\n");
}

}  // namespace test_background_thread_run_in_row
