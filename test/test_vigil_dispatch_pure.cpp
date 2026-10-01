// dispatch_op_pure of test_vigil_dispatch, against dispatch_op.
//
// dispatch_op_pure takes the context of the Vigil's producer claim and must
// otherwise behave exactly like dispatch_op.  The context is not read past
// the call, so every leg has to reach the result that dispatch_op reaches.

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_vigil_dispatch {

using crucible::test::flush_and_wait_region_published;

void test_dispatch_pure_matches_dispatch_op() {
    // A fresh context for each call, and one context for several calls.
    {
        Vigil vigil;
        auto d0 = make_op(0, 0);
        auto r0 = crucible::test::dispatch_pure_synthetic(vigil, vigil.mint_producer_context(), d0.entry, d0.metas,
                                                          d0.n_metas);
        assert(r0.action == DispatchResult::Action::RECORD);

        const VigilFgCtx fg = vigil.mint_producer_context();
        for (uint32_t i = 1; i < 5; ++i) {
            auto d = make_op(0, i);
            auto r = crucible::test::dispatch_pure_synthetic(vigil, fg, d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::RECORD);
        }
    }

    // Alternating the two entry points must not perturb ring order.
    {
        Vigil vigil;
        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(0, i);
            DispatchResult r;
            if (i % 2 == 0) {
                r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
            } else {
                r = crucible::test::dispatch_pure_synthetic(vigil, vigil.mint_producer_context(), d.entry, d.metas,
                                                            d.n_metas);
            }
            assert(r.action == DispatchResult::Action::RECORD);
        }
    }

    {
        Vigil vigil;
        feed_record(vigil, 0);
        feed_record(vigil, 1);
        feed_trigger(vigil, 2);
        flush_and_wait_region_published(vigil);

        const VigilFgCtx fg = vigil.mint_producer_context();
        for (uint32_t i = 0; i < K; ++i) {
            auto d = make_op(3, i);
            auto r = crucible::test::dispatch_pure_synthetic(vigil, fg, d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::RECORD && "alignment via _pure should still RECORD");
        }
        assert(vigil.context().is_compiled() && "CrucibleContext should be compiled after K _pure aligns");

        for (uint32_t i = K; i < NUM_OPS; ++i) {
            auto d = make_op(3, i);
            auto r = crucible::test::dispatch_pure_synthetic(vigil, fg, d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(4, i);
            auto r = crucible::test::dispatch_pure_synthetic(vigil, fg, d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    std::printf("  test_dispatch_pure_matches_dispatch_op: PASSED\n");
}

}  // namespace test_vigil_dispatch
