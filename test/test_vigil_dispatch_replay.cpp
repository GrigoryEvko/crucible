// Replay, divergence and recovery of test_vigil_dispatch, driven through
// dispatch_op.

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_vigil_dispatch {

using crucible::test::flush_and_wait_region_published;

void test_dispatch_basic() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(vigil);

    align_and_activate(vigil, 3);
    assert(vigil.compiled_iterations() == 1);

    uint32_t match_count = 0;
    uint32_t complete_count = 0;
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(4, i);
        auto result = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);

        if (result.status == ReplayStatus::MATCH)
            match_count++;
        else if (result.status == ReplayStatus::COMPLETE)
            complete_count++;
    }

    assert(match_count == NUM_OPS - 1);
    assert(complete_count == 1);
    assert(vigil.compiled_iterations() == 2);

    std::printf("  test_dispatch_basic: PASSED\n");
}

void test_dispatch_divergence() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, 3);

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(4, i);
        auto result = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);
        assert(result.status == ReplayStatus::MATCH);
    }

    // The shape still matches at this position, so only the schema can
    // be what trips the guard.
    TraceRing::Entry bad_entry{};
    bad_entry.schema_hash = SchemaHash{0xBAD};
    bad_entry.shape_hash = SHAPE[3];
    bad_entry.num_inputs = 1;
    bad_entry.num_outputs = 1;
    TensorMeta bad_metas[2]{};
    bad_metas[0] = make_meta(fake_ptr(4, 2));
    bad_metas[1] = make_meta(fake_ptr(4, 3));

    auto result = crucible::test::dispatch_synthetic(vigil, bad_entry, bad_metas, 2);
    assert(result.action == DispatchResult::Action::RECORD);
    assert(result.status == ReplayStatus::DIVERGED);
    assert(vigil.diverged_count() == 1);

    auto d = make_op(4, 4);
    auto result2 = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    assert(result2.action == DispatchResult::Action::RECORD);
    assert(!vigil.context().is_compiled());

    std::printf("  test_dispatch_divergence: PASSED\n");
}

void test_dispatch_recovery() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(vigil);

    align_and_activate(vigil, 3);

    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xBAD};
    bad.shape_hash = SHAPE[0];
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    TensorMeta bad_meta = make_meta(fake_ptr(99, 0));

    auto r_div = crucible::test::dispatch_synthetic(vigil, bad, &bad_meta, 1);
    assert(r_div.action == DispatchResult::Action::RECORD);
    assert(r_div.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    // The ring still holds the alignment and divergence noise, so a
    // couple of clean iterations would not be enough to give the
    // background thread two clean boundaries to work from.  Six is.
    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, iter);
    feed_trigger(vigil, 16);

    // Divergence put the mode back to recording, so this call is waiting
    // for a fresh transition rather than observing the original one.
    flush_and_wait_region_published(vigil);

    align_and_activate(vigil, 17);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(18, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_dispatch_recovery: PASSED\n");
}

}  // namespace test_vigil_dispatch
