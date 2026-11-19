// The pool behind the replay of test_end_to_end: the data flow between the
// slots, and the bounds of each slot.

#include "end_to_end.h"

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "tensor_data.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_end_to_end {

// Each op fills its output with a pattern of its own, so reading the
// expected pattern back through the next op's input is what proves the
// two entries resolve to the same pool bytes.
void test_pipeline_data_flow() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog(::foundation::effects::testing::test().alloc);

    BackgroundThread bt{::foundation::effects::testing::test().alloc};

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, *ring, *meta_log);

    wait_processed(bt, *ring);
    auto* region = bt.active_region.load_acquire().try_get();
    CRUCIBLE_TEST_ASSERT(kTest, region != nullptr);

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[0], SHAPE[0], cv).status() == ReplayStatus::MATCH);
    std::memset(crucible::test::host_ptr(ctx.output_ptr(0, cv)), 0x11, 4096);

    for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[i], SHAPE[i], cv).status() == ReplayStatus::MATCH);

        auto* in_data = crucible::test::host_ptr<uint8_t>(ctx.input_ptr(0, cv));
        uint8_t expected = static_cast<uint8_t>(0x11 + i - 1);
        for (uint32_t b = 0; b < 4096; b++) {
            CRUCIBLE_TEST_ASSERT(kTest, in_data[b] == expected);
        }

        std::memset(crucible::test::host_ptr(ctx.output_ptr(0, cv)), static_cast<int>(0x11 + i), 4096);
    }

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[7], SHAPE[7], cv).status() == ReplayStatus::COMPLETE);
    auto* in_last = crucible::test::host_ptr<uint8_t>(ctx.input_ptr(0, cv));
    uint8_t expected_last = static_cast<uint8_t>(0x11 + NUM_OPS - 2);
    for (uint32_t b = 0; b < 4096; b++) {
        CRUCIBLE_TEST_ASSERT(kTest, in_last[b] == expected_last);
    }

    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    ctx.deactivate();
    bt.stop();
    delete meta_log;
    delete ring;

    ::fixy::check::pass(kTest, "  test_pipeline_data_flow: PASSED\n");
}

void test_pipeline_pool_bounds() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog(::foundation::effects::testing::test().alloc);

    BackgroundThread bt{::foundation::effects::testing::test().alloc};

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, *ring, *meta_log);

    wait_processed(bt, *ring);
    auto* region = bt.active_region.load_acquire().try_get();
    CRUCIBLE_TEST_ASSERT(kTest, region != nullptr);

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));

    auto* pool_base = crucible::test::host_ptr<uint8_t>(ctx.pool().pool_base());
    uint64_t pool_bytes = ctx.pool().pool_bytes();
    CRUCIBLE_TEST_ASSERT(kTest, pool_base != nullptr);
    CRUCIBLE_TEST_ASSERT(kTest, pool_bytes > 0);

    auto cv2 = ctx.mint_compiled_view(kVigilForeground);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv2);
        (void)s;
        auto* p = crucible::test::host_ptr<uint8_t>(ctx.output_ptr(0, cv2));
        CRUCIBLE_TEST_ASSERT(kTest, p >= pool_base);
        CRUCIBLE_TEST_ASSERT(kTest, p + 4096 <= pool_base + pool_bytes);
    }

    // The loop above consumed the iteration, so this one replays it.  No
    // reset is needed: the first advance after COMPLETE rewinds.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv2);
        (void)s;
        if (i > 0) {
            auto* p = crucible::test::host_ptr<uint8_t>(ctx.input_ptr(0, cv2));
            CRUCIBLE_TEST_ASSERT(kTest, p >= pool_base);
            CRUCIBLE_TEST_ASSERT(kTest, p + 4096 <= pool_base + pool_bytes);
        }
    }

    ctx.deactivate();
    bt.stop();
    delete meta_log;
    delete ring;

    ::fixy::check::pass(kTest, "  test_pipeline_pool_bounds: PASSED\n");
}

}  // namespace test_end_to_end
