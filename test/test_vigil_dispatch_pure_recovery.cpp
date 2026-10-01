// dispatch_op_pure of test_vigil_dispatch through a divergence and a
// recovery.

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_vigil_dispatch {

using crucible::test::flush_and_wait_region_published;

// The legs the test of test_vigil_dispatch_pure.cpp does not reach:
// divergence, recovery after divergence, and a context minted while
// recording that the compiled iterations still take.
void test_dispatch_pure_divergence_and_recovery() {
    // Divergence.
    {
        Vigil vigil;
        feed_record(vigil, 0);
        feed_record(vigil, 1);
        feed_trigger(vigil, 2);
        flush_and_wait_region_published(vigil);
        align_and_activate(vigil, 3);

        const VigilFgCtx fg = vigil.mint_producer_context();
        for (uint32_t i = 0; i < 3; ++i) {
            auto d = make_op(4, i);
            auto r = vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
            assert(r.status == ReplayStatus::MATCH);
        }

        // The shape still matches, so only the schema trips the guard.
        TraceRing::Entry bad_entry{};
        bad_entry.schema_hash = SchemaHash{0xBAD};
        bad_entry.shape_hash = SHAPE[3];
        bad_entry.num_inputs = 1;
        bad_entry.num_outputs = 1;
        TensorMeta bad_metas[2]{};
        bad_metas[0] = make_meta(fake_ptr(4, 2));
        bad_metas[1] = make_meta(fake_ptr(4, 3));

        auto rdiv = vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(bad_entry), bad_metas, 2);
        assert(rdiv.action == DispatchResult::Action::RECORD);
        assert(rdiv.status == ReplayStatus::DIVERGED);
        assert(vigil.diverged_count() == 1);
        assert(!vigil.context().is_compiled());

        auto d = make_op(4, 4);
        auto r2 = vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r2.action == DispatchResult::Action::RECORD);
    }

    // Divergence and recovery end to end.
    {
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

        const VigilFgCtx fg = vigil.mint_producer_context();
        auto rdiv = vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(bad), &bad_meta, 1);
        assert(rdiv.action == DispatchResult::Action::RECORD);
        assert(rdiv.status == ReplayStatus::DIVERGED);
        assert(!vigil.context().is_compiled());

        // Six clean iterations, for the same reason the non-wrapper
        // recovery test needs six: the ring still holds the alignment
        // and divergence noise.
        for (uint32_t iter = 10; iter < 16; iter++)
            feed_record(vigil, iter);
        feed_trigger(vigil, 16);
        flush_and_wait_region_published(vigil);

        // Realignment is open-coded rather than delegated to the helper,
        // because the helper drives dispatch_op and this leg has to be
        // driven through the wrapper as well.
        for (uint32_t i = 0; i < K; ++i) {
            auto d = make_op(17, i);
            auto r = vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::RECORD);
        }
        assert(vigil.context().is_compiled());

        for (uint32_t i = K; i < NUM_OPS; ++i) {
            auto d = make_op(17, i);
            auto r = vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(18, i);
            auto r = vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    // A context minted before the first op, while the Vigil records, still
    // serves the compiled iterations after the region activates.
    {
        Vigil vigil;
        const VigilFgCtx fg = vigil.mint_producer_context();
        feed_record(vigil, 0);
        feed_record(vigil, 1);
        feed_trigger(vigil, 2);
        flush_and_wait_region_published(vigil);
        align_and_activate(vigil, 3);

        for (uint32_t iter = 4; iter < 6; ++iter) {
            for (uint32_t i = 0; i < NUM_OPS; ++i) {
                auto d = make_op(iter, i);
                auto r =
                    vigil.dispatch_op_pure(fg, crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
                assert(r.action == DispatchResult::Action::COMPILED);
                assert(r.status == ReplayStatus::MATCH || r.status == ReplayStatus::COMPLETE);
            }
        }
    }

    std::printf("  test_dispatch_pure_divergence_and_recovery: PASSED\n");
}

}  // namespace test_vigil_dispatch
