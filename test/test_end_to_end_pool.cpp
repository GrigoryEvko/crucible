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

namespace {

// The count of the bytes of each tensor of the workload: 1024 floats.
constexpr std::size_t kTensorBytes = 4096;

// Each op fills its output with a pattern of its own, so reading the
// expected pattern back through the next op's input is what proves the
// two entries resolve to the same pool bytes.  The output of an op comes
// through with_output_views, and its input through input_view.
void check_data_flow(crucible::test::TraceRig& /*rig*/, BackgroundThread const& /*bt*/,
                     DagRef<const RegionNode> region) noexcept {
    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    const auto replayed = ctx.with_compiled(context_key, [](CompiledScope& scope) noexcept {
        // The byte that the next write of an output writes, and that the next
        // read of an input expects.
        std::uint8_t pattern = 0x11;
        // Writes pattern into each byte of the output of the matched op.
        const auto is_output_written = [&scope, &pattern]() noexcept {
            return scope
                .with_output_views<std::uint8_t>([&pattern](auto& views) noexcept {
                    std::size_t count = 0;
                    std::move(views).drain([&pattern, &count](auto view) noexcept {
                        for (std::uint8_t& byte : view) {
                            byte = pattern;
                            ++count;
                        }
                    });
                    return count;
                })
                .is_ok_and([](std::size_t const& count) noexcept { return count == kTensorBytes; });
        };
        // True when each byte of the input of the matched op holds pattern.
        const auto is_input_pattern = [&scope, &pattern]() noexcept {
            return scope.input_view<std::uint8_t>(InputPosition{0})
                .is_some_and([&pattern](::fixy::View<std::uint8_t const> const& bytes) noexcept {
                    return bytes.size() == kTensorBytes
                        && bytes.walk().all_of(
                            [&pattern](std::uint8_t const& byte) noexcept { return byte == pattern; });
                });
        };

        CRUCIBLE_TEST_ASSERT(kTest, scope.advance(SCHEMA[0], SHAPE[0]).status() == ReplayStatus::MATCH);
        CRUCIBLE_TEST_ASSERT(kTest, is_output_written());

        for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
            CRUCIBLE_TEST_ASSERT(kTest, scope.advance(SCHEMA[i], SHAPE[i]).status() == ReplayStatus::MATCH);
            CRUCIBLE_TEST_ASSERT(kTest, is_input_pattern());
            pattern = static_cast<std::uint8_t>(0x11 + i);
            CRUCIBLE_TEST_ASSERT(kTest, is_output_written());
        }

        // The last op reads the output of the op before it.
        CRUCIBLE_TEST_ASSERT(kTest, scope.advance(SCHEMA[7], SHAPE[7]).status() == ReplayStatus::COMPLETE);
        CRUCIBLE_TEST_ASSERT(kTest, is_input_pattern());
    });
    CRUCIBLE_TEST_ASSERT(kTest, replayed.is_some());

    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    ctx.deactivate(context_key);
}

// The outputs and the inputs of each op are inside the pool.
// with_output_views refuses an output whose window ends past the last byte
// of the pool (OutsidePool), so each lent output of the tensor bytes is
// inside it, and each input reads the window of the pool of its slot.
void check_pool_bounds(crucible::test::TraceRig& /*rig*/, BackgroundThread const& /*bt*/,
                       DagRef<const RegionNode> region) noexcept {
    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().pool_base().is_some());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().pool_bytes() > 0);

    const auto replayed = ctx.with_compiled(context_key, [](CompiledScope& scope) noexcept {
        // True when the door lends the output of the matched op, with the
        // tensor bytes.
        const auto is_output_inside = [&scope]() noexcept {
            return scope
                .with_output_views<std::uint8_t>([](auto& views) noexcept {
                    std::size_t count = 0;
                    for (auto const& view : views) {
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
            static_cast<void>(scope.advance(SCHEMA[i], SHAPE[i]));
            CRUCIBLE_TEST_ASSERT(kTest, is_output_inside());
        }

        // The loop above consumed the iteration, so this one replays it.  No
        // reset is needed: the first advance after COMPLETE rewinds.
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            static_cast<void>(scope.advance(SCHEMA[i], SHAPE[i]));
            if (i > 0) {
                CRUCIBLE_TEST_ASSERT(kTest,
                                     scope.input_view<std::uint8_t>(InputPosition{0})
                                         .is_some_and([](::fixy::View<std::uint8_t const> const& bytes) noexcept {
                                             return bytes.size() == kTensorBytes;
                                         }));
            }
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, replayed.is_some());

    ctx.deactivate(context_key);
}

}  // namespace

void test_pipeline_data_flow() {
    run_on_first_region(RegionCheck::of<&check_data_flow>());
    ::fixy::check::pass(kTest, "  test_pipeline_data_flow: PASSED\n");
}

void test_pipeline_pool_bounds() {
    run_on_first_region(RegionCheck::of<&check_pool_bounds>());
    ::fixy::check::pass(kTest, "  test_pipeline_pool_bounds: PASSED\n");
}

}  // namespace test_end_to_end
