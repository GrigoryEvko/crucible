// The data that crosses a switch between the two cached variants of
// test_region_cache.

#include "region_cache.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "tensor_data.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_region_cache {

using crucible::test::flush_and_wait_region_published;

// Output written before a switch must survive the move to the other
// region's pool.
void test_cache_data_migration() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        (void)crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    }
    auto dB3 = make_op(SHAPE_B, 1, 4, 3);
    auto r_div = crucible::test::dispatch_synthetic(vigil, dB3.entry, dB3.metas, dB3.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, r_div.action == DispatchResult::Action::RECORD);

    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    CRUCIBLE_TEST_ASSERT(kTest, vigil.region_cache().size() == 2);

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, r.action == DispatchResult::Action::COMPILED);
        // A prefix output holds 1024 floats, which is 4096 bytes.
        std::memset(crucible::test::host_ptr(vigil.output_ptr(vigil.mint_producer_context(), 0)),
                    static_cast<int>(0xA0 + i), 4096);
    }

    auto dA3 = make_op(SHAPE_A, 0, 18, 3);
    auto r_switch = crucible::test::dispatch_synthetic(vigil, dA3.entry, dA3.metas, dA3.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, r_switch.action == DispatchResult::Action::COMPILED
                                    && "Expected cache switch from B to A at pos 3");
    // The switch replays the prefix and then matches op 3 of A, so the
    // result names op 3.
    CRUCIBLE_FATAL_INVARIANT(r_switch.compiled_op_index() == OpIndex{3});

    // Op 3's input is op 2's output, so it must still carry the pattern
    // written for i equal to two.
    auto* in_data = crucible::test::host_ptr<uint8_t>(vigil.input_ptr(vigil.mint_producer_context(), 0));
    for (uint32_t b = 0; b < 4096; b++) {
        CRUCIBLE_TEST_ASSERT(kTest, in_data[b] == 0xA2 && "Data migration failed: op 2's output not preserved");
    }

    for (uint32_t i = 4; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 18, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, r.action == DispatchResult::Action::COMPILED);
        CRUCIBLE_FATAL_INVARIANT(r.compiled_op_index() == OpIndex{i});
    }

    ::fixy::check::pass(kTest, "  test_cache_data_migration: PASSED\n");
}

}  // namespace test_region_cache
