// A second boundary of test_end_to_end.

#include "end_to_end.h"

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_end_to_end {

// The second boundary costs less than the first.  Once the signature is
// confirmed the next match fires immediately, and the background thread
// keeps the K trigger ops as the opening of the next trace rather than
// discarding them.  So the feed here completes that partial iteration
// before starting a fresh one.
void test_pipeline_multi_iteration() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog(::foundation::effects::testing::test().alloc);

    BackgroundThread bt{::foundation::effects::testing::test().alloc};

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, *ring, *meta_log);

    wait_processed(bt, *ring);
    auto* region1 = bt.active_region.load_acquire().try_get();
    CRUCIBLE_TEST_ASSERT(kTest, region1 != nullptr);
    CRUCIBLE_TEST_ASSERT(kTest, region1->num_ops() == NUM_OPS);

    // The loop starts at K because the background thread already holds
    // the first K ops of this iteration from the trigger feed above.
    for (uint32_t i = IterationDetector::K; i < NUM_OPS; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SCHEMA[i];
        e.shape_hash = SHAPE[i];
        e.num_inputs = 1;
        e.num_outputs = 1;

        const ::fixy::FixedArray<TensorMeta, 2> metas{make_meta(fake_ptr(2, i - 1)), make_meta(fake_ptr(2, i))};

        auto ms = meta_log->try_append(metas.view());
        CRUCIBLE_TEST_ASSERT(kTest, ms.is_valid());
        CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(e, ms));
    }

    feed_iteration(ring, meta_log, 3);
    feed_trigger(ring, meta_log, 4);

    wait_processed(bt, *ring);
    auto* region2 = bt.active_region.load_acquire().try_get();
    CRUCIBLE_TEST_ASSERT(kTest, region2 != nullptr && region2 != region1 && "Second boundary did not fire");
    CRUCIBLE_TEST_ASSERT(kTest, region2->num_ops() == NUM_OPS);
    CRUCIBLE_TEST_ASSERT(kTest, region2->plan.is_some());

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region2));
    auto cv = ctx.mint_compiled_view(kVigilForeground);
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv);
        if (i < NUM_OPS - 1) {
            CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
        } else {
            CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::COMPLETE);
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    ctx.deactivate();
    bt.stop();
    delete meta_log;
    delete ring;

    ::fixy::check::pass(kTest, "  test_pipeline_multi_iteration: PASSED\n");
}

}  // namespace test_end_to_end
