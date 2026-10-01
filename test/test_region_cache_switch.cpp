// A switch between two cached variants of test_region_cache, and a lookup
// that misses.

#include "region_cache.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_region_cache {

using crucible::test::flush_and_wait_region_published;

// Once both variants are cached, a switch between them at the first
// diverging op must come out of the cache.
void test_cache_switch_mid_iter() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    assert(vigil.region_cache().size() == 1);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 5, i);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
    auto dB3 = make_op(SHAPE_B, 1, 5, 3);
    auto r_div = vigil.dispatch_op(crucible::test::certify_synthetic_entry(dB3.entry), dB3.metas, dB3.n_metas);
    assert(r_div.action == DispatchResult::Action::RECORD);
    assert(r_div.status == ReplayStatus::DIVERGED);

    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    assert(vigil.region_cache().size() == 2);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    // Ops 0 to 2 are the shared prefix, so the switch back to A can only
    // show up at op 3.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 19, i);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    auto dA3 = make_op(SHAPE_A, 0, 19, 3);
    auto r_switch = vigil.dispatch_op(crucible::test::certify_synthetic_entry(dA3.entry), dA3.metas, dA3.n_metas);

    assert(r_switch.action == DispatchResult::Action::COMPILED && "Expected instant cache switch to variant A");

    for (uint32_t i = 4; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 19, i);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_cache_switch_mid_iter: PASSED\n");
}

void test_cache_miss_fallback() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    // A schema no cached region carries, so the lookup must miss.
    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xDEAD};
    bad.shape_hash = ShapeHash{0xBEEF};
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    TensorMeta bad_meta = make_meta(fake_ptr(99, 99, 0), 512);

    auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(bad), &bad_meta, 1);
    assert(r.action == DispatchResult::Action::RECORD);
    assert(r.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());
    assert(vigil.diverged_count() == 1);

    assert(vigil.region_cache().size() == 1);

    std::printf("  test_cache_miss_fallback: PASSED\n");
}

}  // namespace test_region_cache
