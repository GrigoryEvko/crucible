// A stream that starts with ops that never occur again still compiles its
// loop, and the background pipeline holds a bounded trace while it searches.
//
// The foreground of the test waits for the pipeline many times.  Each wait
// sleeps between two reads of a counter of the pipeline, and it does not
// spin.  A spin runs instructions for each moment that the pipeline needs,
// so its count increases with the load of the host.  The launcher of the
// test limits the user instructions of the test.

#include <crucible/BackgroundThread.h>
#include <crucible/IterationDetector.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <fixy/FixedArray.h>
#include <fixy/os/Time.h>

#include "test_assert.h"
#include "test_harness.h"
#include <bit>
#include <cstdint>

using crucible::BackgroundThread;
using crucible::IterationDetector;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;
using crucible::Vigil;

namespace {

// The pause between two reads of a counter of the pipeline, in nanoseconds.
constexpr uint64_t kPollNanos = 100000;

// Waits until the background pipeline processed each op that the ring
// holds, as Vigil::flush does.  The foreground is the one producer of the
// ring, so the ops of the ring do not change during the wait.
void wait_for_pipeline(const Vigil& vigil) {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kPollNanos>(runner);
    while (!vigil.flush_complete())
        (void)pause.sleep_for(kPollNanos);
}

crucible::TensorMeta make_meta(uint64_t address) noexcept {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(1024);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.data_ptr = crucible::external_data_ptr(std::bit_cast<void*>(address));
    return meta;
}

// Records one op with one input and one output.  Returns false when the
// trace ring refused the op.
bool record(Vigil& vigil, uint64_t schema, uint64_t shape, uint64_t input_address, uint64_t output_address) {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema};
    entry.shape_hash = crucible::ShapeHash{shape};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    ::fixy::FixedArray<crucible::TensorMeta, 2> metas{};
    metas[0] = make_meta(input_address);
    metas[1] = make_meta(output_address);
    return vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), 2);
}

// The records of each op of a long prefix that fills the metadata log: one
// input and five outputs.
constexpr uint16_t kWideRecords = 6;

// Records one op of the prefix with kWideRecords records.  Returns true when
// the metadata log refused the records.  The foreground is the one writer
// of the head of the log, so the head moves only in this call.
bool record_wide(Vigil& vigil, uint64_t schema, uint64_t shape, uint64_t address) {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema};
    entry.shape_hash = crucible::ShapeHash{shape};
    entry.num_inputs = 1;
    entry.num_outputs = kWideRecords - 1;
    ::fixy::FixedArray<crucible::TensorMeta, kWideRecords> metas{};
    for (uint16_t index = 0; index < kWideRecords; ++index)
        metas[index] = make_meta(address + uint64_t{index} * 0x40);
    const crucible::MetaLog& log = vigil.meta_log(vigil.mint_producer_context());
    const uint64_t head_before = log.head.get();
    (void)vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), kWideRecords);
    return log.head.get() == head_before;
}

// Records the eight-op loop until it publishes a region or until 400
// iterations.  Returns the iterations of the loop that the region took, or
// zero when no region came.
uint32_t iterations_of_loop_to_region(Vigil& vigil) {
    wait_for_pipeline(vigil);
    for (uint32_t iteration = 0; iteration < 400; ++iteration) {
        const uint64_t base = 0x40000000 + uint64_t{iteration} * 0x10000;
        for (uint64_t op = 0; op < 8; ++op)
            (void)record(vigil, 0x100 + op, 0x200 + op, base + op * 0x100, base + (op + 1) * 0x100);
        wait_for_pipeline(vigil);
        if (vigil.active_region() != nullptr) return iteration + 1;
    }
    return 0;
}

// Records ops that never occur again, then the eight-op loop until it
// publishes a region or until 400 iterations.  Returns the iterations of the
// loop that the region took, or zero when no region came.
uint32_t iterations_to_region(Vigil& vigil, uint64_t prefix_ops) {
    for (uint64_t i = 0; i < prefix_ops; ++i) {
        (void)record(vigil, 0x1000000 + i, 0x2000000 + i, 0x100000 + i * 0x100, 0x100080 + i * 0x100);
        if ((i & 0x3fff) == 0) wait_for_pipeline(vigil);
    }
    return iterations_of_loop_to_region(vigil);
}

void test_prefix_that_never_recurs_publishes_a_region() {
    // Five ops before the loop, as an initialization records them.  The
    // region comes a few iterations after the loop starts.
    Vigil vigil;
    const uint32_t iterations = iterations_to_region(vigil, 5);
    assert(iterations != 0 && "the loop after the prefix published no region");
    assert(iterations <= 8);
    assert(vigil.bg_detector_boundaries() > 0);
}

void test_prefix_of_more_records_than_the_log_publishes_a_region() {
    // A prefix of 600,000 ops with two records each puts 1.2 M records in
    // the log, which holds 2^20.  The commit markers of the pipeline release
    // each record that no later iteration can claim, so the log has room
    // when the loop starts, and the loop publishes its region.
    Vigil vigil;
    const uint32_t iterations = iterations_to_region(vigil, 600000);
    assert(iterations != 0 && "the loop after 1.2 M records of a prefix published no region");
    assert(iterations <= 8);
}

void test_log_that_fills_is_released_again() {
    // The detect stage holds about 2^18 ops of a prefix that it searches.
    // At six records for each op, those ops hold more records than the log,
    // so the log fills and refuses ops.  The floor still passes each op that
    // no iteration can claim, so the log has room again, and the loop after
    // the prefix publishes its region.
    Vigil vigil;
    uint64_t refused_ops = 0;
    for (uint64_t i = 0; i < 600000; ++i) {
        if (record_wide(vigil, 0x5000000 + i, 0x6000000 + i, 0x10000000 + i * 0x200)) ++refused_ops;
        if ((i & 0x3fff) == 0) wait_for_pipeline(vigil);
    }
    assert(refused_ops > 0 && "the prefix did not fill the metadata log");
    const uint32_t iterations = iterations_of_loop_to_region(vigil);
    assert(iterations != 0 && "the loop after a full metadata log published no region");
    const crucible::MetaLog& log = vigil.meta_log(vigil.mint_producer_context());
    assert(log.head.get() - log.tail.get() < MetaLog::CAPACITY / 2 && "the log stayed full after the loop");
}

// The trace ring and the metadata log of the pipeline test.  Each is a
// function-local object, made at the first call.
TraceRing& pipeline_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& pipeline_log() {
    static MetaLog log{};
    return log;
}

void test_detect_stage_holds_a_bounded_trace() {
    // Ops that never occur again give no period.  The detect stage holds
    // only the ops that a future iteration can claim: at most half of the
    // history of the detector and K ops more.  The stream fills the largest
    // history, and then it moves through a second full history.
    BackgroundThread bt;
    TraceRing& ring = pipeline_ring();
    bt.start(&ring, &pipeline_log());
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kPollNanos>(runner);
    constexpr uint64_t kOps = uint64_t{2} * IterationDetector::HISTORY_MAX_CAPACITY;
    for (uint64_t i = 0; i < kOps; ++i) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{0x3000000 + i};
        entry.shape_hash = crucible::ShapeHash{0x4000000 + i};
        while (!ring.try_append(entry, MetaIndex::none()))
            (void)pause.sleep_for(kPollNanos);
    }
    const uint64_t target = ring.total_produced();
    while (bt.total_processed.load_acquire() < target)
        (void)pause.sleep_for(kPollNanos);
    assert(bt.pending_op_count() <= IterationDetector::HISTORY_MAX_CAPACITY / 2 + IterationDetector::K);
    assert(bt.detector.pos_ - bt.detector.base_ <= IterationDetector::HISTORY_MAX_CAPACITY);
    bt.stop();
}

}  // namespace

int main() {
    test_prefix_that_never_recurs_publishes_a_region();
    test_prefix_of_more_records_than_the_log_publishes_a_region();
    test_log_that_fills_is_released_again();
    test_detect_stage_holds_a_bounded_trace();
    return 0;
}
