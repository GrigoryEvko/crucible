// A region, its replay and a divergence of test_end_to_end.

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

namespace {

void check_replay(crucible::test::TraceRig& /*rig*/, BackgroundThread const& /*bt*/,
                  DagRef<const RegionNode> region) noexcept {
    CRUCIBLE_TEST_ASSERT(kTest, region->kind == TraceNodeKind::REGION);
    CRUCIBLE_TEST_ASSERT(kTest, region->num_ops() == NUM_OPS);
    CRUCIBLE_TEST_ASSERT(kTest, region->plan.is_some());
    for (const DagRef<MemoryPlan const>& plan : region->plan) {
        CRUCIBLE_TEST_ASSERT(kTest, plan->pool_bytes > 0);
        CRUCIBLE_TEST_ASSERT(kTest, plan->num_slots() > 0);
    }

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        CRUCIBLE_TEST_ASSERT(kTest, region->op_at(i).schema_hash == SCHEMA[i]);
        CRUCIBLE_TEST_ASSERT(kTest, region->op_at(i).shape_hash == SHAPE[i]);
        CRUCIBLE_TEST_ASSERT(kTest, region->op_at(i)
                                        .output_slot_ids()
                                        .get(0)
                                        .expect("a test reads an element at or past the count of its View")
                                        ->is_valid());
    }

    // The feed never states an edge.  Matching slots here mean the
    // planner recovered the producer-consumer edge from the shared data
    // pointer alone.
    for (uint32_t i = 1; i < NUM_OPS; i++) {
        CRUCIBLE_TEST_ASSERT(kTest, *region->op_at(i).input_slot_ids().get(0).expect(
                                        "a test reads an element at or past the count of its View")
                                        == *region->op_at(i - 1).output_slot_ids().get(0).expect(
                                            "a test reads an element at or past the count of its View"));
    }

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(ctx.active_region(), ::fixy::some(region)));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().is_initialized());

    auto cv = ctx.mint_compiled_view(kVigilForeground);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        const ReplayStatus s = ctx.advance(SCHEMA[i], SHAPE[i], cv).status();
        if (i < NUM_OPS - 1) {
            CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
        } else {
            CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::COMPLETE);
        }
        CRUCIBLE_TEST_ASSERT(kTest, ctx.output_ptr(0, cv).is_some());
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 1);

    // No explicit reset: the first advance after COMPLETE rewinds.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        const ReplayStatus s = ctx.advance(SCHEMA[i], SHAPE[i], cv).status();
        if (i < NUM_OPS - 1) {
            CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
        } else {
            CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::COMPLETE);
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 0);

    ctx.deactivate();
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());
}

void check_divergence(crucible::test::TraceRig& /*rig*/, BackgroundThread const& /*bt*/,
                      DagRef<const RegionNode> region) noexcept {
    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(*region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    for (uint32_t i = 0; i < 3; i++) {
        CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[i], SHAPE[i], cv).status() == ReplayStatus::MATCH);
    }

    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SchemaHash{0xBAD}, SHAPE[3], cv).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 1);
    // A divergence does not switch modes.  Choosing a fallback is the
    // caller's decision, not the context's.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());

    // The position did not move, so op 3 is still the one under test.
    // This time the schema matches and the shape does not.
    CRUCIBLE_TEST_ASSERT(kTest, ctx.advance(SCHEMA[3], ShapeHash{0xBAD}, cv).status() == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 2);

    ctx.deactivate();
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_recording());
}

}  // namespace

void test_pipeline_basic() {
    run_on_first_region(RegionCheck::of<&check_replay>());
    ::fixy::check::pass(kTest, "  test_pipeline_basic: PASSED\n");
}

void test_pipeline_divergence() {
    run_on_first_region(RegionCheck::of<&check_divergence>());
    ::fixy::check::pass(kTest, "  test_pipeline_divergence: PASSED\n");
}

}  // namespace test_end_to_end
