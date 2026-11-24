// Replay, divergence and recovery of test_vigil_dispatch, driven through
// dispatch_op.

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstdint>
#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_vigil_dispatch {

using crucible::test::flush_and_wait_region_published;

void test_dispatch_basic() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(kTest, vigil);

    align_and_activate(vigil, 3);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.compiled_iterations() == 1);

    uint32_t match_count = 0;
    uint32_t complete_count = 0;
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(4, i);
        auto result = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, result.action == DispatchResult::Action::COMPILED);
        // Each compiled result names its op, and the COMPLETE of the last op
        // names the last op, although the reset put the cursor on the first.
        CRUCIBLE_FATAL_INVARIANT(result.compiled_op_index() == OpIndex{i});

        if (result.status == ReplayStatus::MATCH)
            match_count++;
        else if (result.status == ReplayStatus::COMPLETE)
            complete_count++;
    }

    CRUCIBLE_TEST_ASSERT(kTest, match_count == NUM_OPS - 1);
    CRUCIBLE_TEST_ASSERT(kTest, complete_count == 1);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.compiled_iterations() == 2);

    // The recorded op of a divergence carries no op index.
    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xBAD};
    bad.shape_hash = SHAPE[0];
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    const ::fixy::FixedArray<TensorMeta, 1> bad_meta{make_meta(fake_ptr(5, 0))};
    const DispatchResult recorded = crucible::test::dispatch_synthetic(vigil, bad, bad_meta, 1);
    CRUCIBLE_FATAL_INVARIANT(recorded.is_record() && !recorded.op_index.is_valid());

    ::fixy::check::pass(kTest, "  test_dispatch_basic: PASSED\n");
}

void test_dispatch_divergence() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(kTest, vigil);
    align_and_activate(vigil, 3);

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(4, i);
        auto result = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, result.action == DispatchResult::Action::COMPILED);
        CRUCIBLE_TEST_ASSERT(kTest, result.status == ReplayStatus::MATCH);
    }

    // The shape still matches at this position, so only the schema can
    // be what trips the guard.
    TraceRing::Entry bad_entry{};
    bad_entry.schema_hash = SchemaHash{0xBAD};
    bad_entry.shape_hash = SHAPE[3];
    bad_entry.num_inputs = 1;
    bad_entry.num_outputs = 1;
    ::fixy::FixedArray<TensorMeta, 2> bad_metas{};
    bad_metas.set(::fixy::index<0>(), make_meta(fake_ptr(4, 2)));
    bad_metas.set(::fixy::index<1>(), make_meta(fake_ptr(4, 3)));

    auto result = crucible::test::dispatch_synthetic(vigil, bad_entry, bad_metas, 2);
    CRUCIBLE_TEST_ASSERT(kTest, result.action == DispatchResult::Action::RECORD);
    CRUCIBLE_TEST_ASSERT(kTest, result.status == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.diverged_count() == 1);

    auto d = make_op(4, 4);
    auto result2 = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, result2.action == DispatchResult::Action::RECORD);
    CRUCIBLE_TEST_ASSERT(kTest, !vigil.context().is_compiled());

    ::fixy::check::pass(kTest, "  test_dispatch_divergence: PASSED\n");
}

void test_dispatch_recovery() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(kTest, vigil);

    align_and_activate(vigil, 3);

    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xBAD};
    bad.shape_hash = SHAPE[0];
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    const ::fixy::FixedArray<TensorMeta, 1> bad_meta{make_meta(fake_ptr(99, 0))};

    auto r_div = crucible::test::dispatch_synthetic(vigil, bad, bad_meta, 1);
    CRUCIBLE_TEST_ASSERT(kTest, r_div.action == DispatchResult::Action::RECORD);
    CRUCIBLE_TEST_ASSERT(kTest, r_div.status == ReplayStatus::DIVERGED);
    CRUCIBLE_TEST_ASSERT(kTest, !vigil.context().is_compiled());

    // The ring still holds the alignment and divergence noise, so a
    // couple of clean iterations would not be enough to give the
    // background thread two clean boundaries to work from.  Six is.
    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, iter);
    feed_trigger(vigil, 16);

    // Divergence put the mode back to recording, so this call is waiting
    // for a fresh transition rather than observing the original one.
    flush_and_wait_region_published(kTest, vigil);

    align_and_activate(vigil, 17);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(18, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, r.action == DispatchResult::Action::COMPILED);
    }

    ::fixy::check::pass(kTest, "  test_dispatch_recovery: PASSED\n");
}

}  // namespace test_vigil_dispatch
