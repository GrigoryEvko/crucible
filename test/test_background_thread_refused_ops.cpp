// A trace ring that refuses ops gives no region of a wrong iteration.
//
// The foreground records with no wait, so a full ring refuses an op, and the
// op leaves no mark in the ring.  The stream that the pipeline reads then has
// a gap.  The test records more ops than the ring holds before the pipeline
// starts, so no stage drains the ring.  The gap is not a whole number of
// iterations, so one iteration that the pipeline reads misses some ops.  The
// queue of uncompiled regions holds the last regions that the pipeline
// published, those around the gap too.  The ops of each must be one
// iteration of the loop, each op in the order of the loop.

#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

#include "test_assert.h"
#include "test_harness.h"
#include <cstdint>

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;

namespace {

// The ops of one iteration of the loop.  The count does not divide the
// capacity of the ring, so a gap cuts an iteration.
constexpr uint64_t kPeriod = 7;
// The ops that the full ring refuses.  The count is not a multiple of the
// period.
constexpr uint64_t kRefused = 1000;
constexpr uint64_t kSchemaBase = 0x100;
constexpr uint64_t kPollNanos = 100000;
// The iterations after the gap.  The queue of uncompiled regions holds 64,
// so it holds each region after the gap and the regions just before it.
constexpr uint64_t kIterationsAfterGap = 40;

[[nodiscard]] bool is_one_iteration(::fixy::View<crucible::TraceEntry> ops) noexcept {
    if (ops.size() != kPeriod) return false;
    bool is_first = true;
    uint64_t expected = 0;
    for (const crucible::TraceEntry& op : ops) {
        const uint64_t position = op.schema_hash.raw() - kSchemaBase;
        if (position >= kPeriod) return false;
        if (!is_first && position != expected) return false;
        is_first = false;
        expected = (position + 1) % kPeriod;
    }
    return true;
}

TraceRing& test_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& test_log() {
    static MetaLog log{};
    return log;
}

// Records the op at position index of the endless loop.  Returns false when
// the ring refused it.
bool record(TraceRing& ring, uint64_t index) {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{kSchemaBase + index % kPeriod};
    entry.shape_hash = crucible::ShapeHash{0x200 + index % kPeriod};
    return ring.try_append(entry, MetaIndex::none());
}

// Waits until the pipeline processed each op of the ring.
void wait_for_pipeline(BackgroundThread& bt, TraceRing& ring) {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kPollNanos>(runner);
    while (bt.total_processed.load_acquire() < ring.total_produced())
        (void)pause.sleep_for(kPollNanos);
}

void test_refused_ops_give_no_wrong_region() {
    TraceRing& ring = test_ring();
    BackgroundThread bt;

    // No pipeline runs, so the ring takes CAPACITY ops of the loop and
    // refuses the rest.  The program goes on, so the index counts each
    // refused op.
    uint64_t index = 0;
    uint64_t refused = 0;
    for (const uint64_t end = TraceRing::CAPACITY + kRefused; index < end; ++index) {
        if (!record(ring, index)) ++refused;
    }
    assert(refused == kRefused);

    // The pipeline publishes the regions of the iterations in the full ring.
    bt.start(&ring, &test_log());
    wait_for_pipeline(bt, ring);
    const uint64_t regions_before_gap = bt.uncompiled_regions.total();

    // The loop goes on after the gap, and the pipeline reads the stream with
    // the gap.
    for (const uint64_t end = index + kIterationsAfterGap * kPeriod; index < end; ++index)
        assert(record(ring, index));
    wait_for_pipeline(bt, ring);
    bt.stop();

    assert(regions_before_gap > 0 && "the loop before the gap published no region");
    assert(bt.uncompiled_regions.total() > regions_before_gap && "the loop after the gap published no region");
    // The pipeline is stopped, so the queue can be read.
    for (uint32_t age = 0; age < bt.uncompiled_regions.size(); ++age) {
        assert(is_one_iteration(bt.uncompiled_regions.at(age).get().ops)
               && "a region holds ops that are not one iteration of the loop");
    }
}

}  // namespace

int main() {
    test_refused_ops_give_no_wrong_region();
    return 0;
}
