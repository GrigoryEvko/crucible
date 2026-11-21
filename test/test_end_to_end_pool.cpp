// The pool behind the replay of test_end_to_end: the data flow between the
// slots, and the bounds of each slot.

#include "end_to_end.h"

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_end_to_end {

// The count of the bytes of each tensor of the workload: 1024 floats.
constexpr std::size_t kTensorBytes = 4096;

// Each op fills its output with a pattern of its own, so reading the
// expected pattern back through the next op's input is what proves the
// two entries resolve to the same pool bytes.  The output of an op comes
// through with_output_views, and its input through input_view.
void test_pipeline_data_flow() {
    ::fixy::Box<TraceRing> ring = ::fixy::mint_box<TraceRing>(::foundation::effects::testing::test().alloc);
    ::fixy::Box<MetaLog> meta_log = ::fixy::mint_box<MetaLog>(::foundation::effects::testing::test().alloc,
                                                              ::foundation::effects::testing::test().alloc);

    BackgroundThread bt{::foundation::effects::testing::test().alloc};

    feed_iteration(*ring, *meta_log, Iteration{0});
    feed_iteration(*ring, *meta_log, Iteration{1});
    feed_trigger(*ring, *meta_log, Iteration{2});

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, *ring, *meta_log);

    wait_processed(bt, *ring);
    const ::fixy::Ref<const RegionNode> region = bt.active_region().expect("BackgroundThread did not produce a region");

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    // The byte that the next write of an output writes, and that the next
    // read of an input expects.
    std::uint8_t pattern = 0x11;
    // Writes pattern into each byte of the output of the matched op.
    const auto is_output_written = [&ctx, &cv, &pattern]() noexcept {
        return ctx
            .with_output_views<std::uint8_t>(cv,
                                             [&pattern](auto& views) noexcept {
                                                 std::size_t count = 0;
                                                 for (auto& view : views) {
                                                     for (std::uint8_t& byte : view) {
                                                         byte = pattern;
                                                         ++count;
                                                     }
                                                 }
                                                 return count;
                                             })
            .is_ok_and([](std::size_t const& count) noexcept { return count == kTensorBytes; });
    };
    // True when each byte of the input of the matched op holds pattern.
    const auto is_input_pattern = [&ctx, &cv, &pattern]() noexcept {
        return ctx.input_view<std::uint8_t>(InputPosition{0}, cv)
            .is_some_and([&pattern](::fixy::View<std::uint8_t const> const& bytes) noexcept {
                return bytes.size() == kTensorBytes
                    && bytes.walk().all_of([&pattern](std::uint8_t const& byte) noexcept { return byte == pattern; });
            });
    };

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[0], SHAPE[0], cv).status() == ReplayStatus::MATCH);
    CRUCIBLE_TEST_ASSERT(kTest, is_output_written());

    for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[i], SHAPE[i], cv).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, is_input_pattern());
        pattern = static_cast<std::uint8_t>(0x11 + i);
        CRUCIBLE_TEST_ASSERT(kTest, is_output_written());
    }

    // The last op reads the output of the op before it.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[7], SHAPE[7], cv).status() == ReplayStatus::COMPLETE);
    CRUCIBLE_TEST_ASSERT(kTest, is_input_pattern());

    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    ctx.deactivate();
    bt.stop();

    ::fixy::check::pass(kTest, "  test_pipeline_data_flow: PASSED\n");
}

// The outputs and the inputs of each op are inside the pool.
// with_output_views refuses an output whose window ends past the last byte
// of the pool (OutsidePool), so each lent output of the tensor bytes is
// inside it, and each input reads the window of the pool of its slot.
void test_pipeline_pool_bounds() {
    ::fixy::Box<TraceRing> ring = ::fixy::mint_box<TraceRing>(::foundation::effects::testing::test().alloc);
    ::fixy::Box<MetaLog> meta_log = ::fixy::mint_box<MetaLog>(::foundation::effects::testing::test().alloc,
                                                              ::foundation::effects::testing::test().alloc);

    BackgroundThread bt{::foundation::effects::testing::test().alloc};

    feed_iteration(*ring, *meta_log, Iteration{0});
    feed_iteration(*ring, *meta_log, Iteration{1});
    feed_trigger(*ring, *meta_log, Iteration{2});

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, *ring, *meta_log);

    wait_processed(bt, *ring);
    const ::fixy::Ref<const RegionNode> region = bt.active_region().expect("BackgroundThread did not produce a region");

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().pool_base().is_some());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().pool_bytes() > 0);

    auto cv2 = ctx.mint_compiled_view(kVigilForeground);

    // True when the door lends the output of the matched op, with the tensor
    // bytes.
    const auto is_output_inside = [&ctx, &cv2]() noexcept {
        return ctx
            .with_output_views<std::uint8_t>(cv2,
                                             [](auto& views) noexcept {
                                                 std::size_t count = 0;
                                                 for (auto& view : views) {
                                                     for (std::uint8_t const& byte : view) {
                                                         static_cast<void>(byte);
                                                         ++count;
                                                     }
                                                 }
                                                 return count;
                                             })
            .is_ok_and([](std::size_t const& count) noexcept { return count == kTensorBytes; });
    };

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        static_cast<void>(ctx.advance(SCHEMA[i], SHAPE[i], cv2));
        CRUCIBLE_TEST_ASSERT(kTest, is_output_inside());
    }

    // The loop above consumed the iteration, so this one replays it.  No
    // reset is needed: the first advance after COMPLETE rewinds.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        static_cast<void>(ctx.advance(SCHEMA[i], SHAPE[i], cv2));
        if (i > 0) {
            CRUCIBLE_TEST_ASSERT(kTest, ctx.input_view<std::uint8_t>(InputPosition{0}, cv2)
                                            .is_some_and([](::fixy::View<std::uint8_t const> const& bytes) noexcept {
                                                return bytes.size() == kTensorBytes;
                                            }));
        }
    }

    ctx.deactivate();
    bt.stop();

    ::fixy::check::pass(kTest, "  test_pipeline_pool_bounds: PASSED\n");
}

}  // namespace test_end_to_end
