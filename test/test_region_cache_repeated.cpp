// Switches back and forth between the two cached variants of
// test_region_cache.

#include "region_cache.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_region_cache {

using crucible::test::flush_and_wait_region_published;

// This is the alternating-batch-size case.  Once both regions are
// cached, every switch must come from the cache and none may fall back
// to recording.
void test_cache_repeated_switching() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        (void)crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    }

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 5, i);
        (void)crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    }
    auto dB3 = make_op(SHAPE_B, 1, 5, 3);
    (void)crucible::test::dispatch_synthetic(vigil, dB3.entry, dB3.metas, dB3.n_metas);

    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    assert(vigil.region_cache().size() == 2);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        (void)crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    }

    for (uint32_t cycle = 0; cycle < 4; cycle++) {
        const ShapeHash* active_shapes = (cycle % 2 == 0) ? SHAPE_A : SHAPE_B;
        uint32_t variant = (cycle % 2 == 0) ? 0 : 1;
        uint32_t iter = 20 + cycle;

        for (uint32_t i = 0; i < 3; i++) {
            auto d = make_op(active_shapes, variant, iter, i);
            auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        auto d3 = make_op(active_shapes, variant, iter, 3);
        auto r3 = crucible::test::dispatch_synthetic(vigil, d3.entry, d3.metas, d3.n_metas);
        assert(r3.action == DispatchResult::Action::COMPILED && "Cache switch failed during repeated alternation");

        for (uint32_t i = 4; i < NUM_OPS; i++) {
            auto d = make_op(active_shapes, variant, iter, i);
            auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    crucible::test::pass("  test_cache_repeated_switching: PASSED\n");
}

}  // namespace test_region_cache
