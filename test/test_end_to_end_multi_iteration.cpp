// A second boundary of test_end_to_end.

#include "end_to_end.h"

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include "test_assert.h"
#include <atomic>
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_end_to_end {

// The second boundary costs less than the first.  Once the signature is
// confirmed the next match fires immediately, and the background thread
// keeps the K trigger ops as the opening of the next trace rather than
// discarding them.  So the feed here completes that partial iteration
// before starting a fresh one.
void test_pipeline_multi_iteration() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog();

    BackgroundThread bt;

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(ring, meta_log);

    wait_processed(bt, *ring);
    auto* region1 = bt.active_region.load(std::memory_order_acquire);
    assert(region1 != nullptr);
    assert(region1->num_ops == NUM_OPS);

    // The loop starts at K because the background thread already holds
    // the first K ops of this iteration from the trigger feed above.
    for (uint32_t i = IterationDetector::K; i < NUM_OPS; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SCHEMA[i];
        e.shape_hash = SHAPE[i];
        e.num_inputs = 1;
        e.num_outputs = 1;

        TensorMeta metas[2]{};
        metas[0] = make_meta(fake_ptr(2, i - 1));
        metas[1] = make_meta(fake_ptr(2, i));

        auto ms = meta_log->try_append(metas, 2);
        assert(ms.is_valid());
        assert(ring->try_append(e, ms));
    }

    feed_iteration(ring, meta_log, 3);
    feed_trigger(ring, meta_log, 4);

    wait_processed(bt, *ring);
    auto* region2 = bt.active_region.load(std::memory_order_acquire);
    assert(region2 != nullptr && region2 != region1 && "Second boundary did not fire");
    assert(region2->num_ops == NUM_OPS);
    assert(region2->plan != nullptr);

    CrucibleContext ctx;
    assert(ctx.activate(region2));
    auto cv = ctx.mint_compiled_view(kVigilForeground);
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv);
        if (i < NUM_OPS - 1) {
            assert(s == ReplayStatus::MATCH);
        } else {
            assert(s == ReplayStatus::COMPLETE);
        }
    }
    assert(ctx.compiled_iterations() == 1);

    ctx.deactivate();
    bt.stop();
    delete meta_log;
    delete ring;

    std::printf("  test_pipeline_multi_iteration: PASSED\n");
}

}  // namespace test_end_to_end
