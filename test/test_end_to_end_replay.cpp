// A region, its replay and a divergence of test_end_to_end.

#include "end_to_end.h"

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include "test_assert.h"
#include <atomic>
#include <cstdint>
#include <cstdio>

using namespace crucible;

namespace test_end_to_end {

// A boundary needs two matches to confirm, which is why three feeds are
// required rather than two.  The first iteration builds the signature,
// the second produces a candidate match, and the third's K-th matching
// op confirms the boundary.
void test_pipeline_basic() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog();

    BackgroundThread bt;

    // Everything is queued before the background thread starts, so the
    // drain sees one complete stream and the outcome does not depend on
    // how the two threads interleave.
    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(ring, meta_log);

    wait_processed(bt, *ring);
    auto* region = bt.active_region.load(std::memory_order_acquire);
    assert(region != nullptr && "BackgroundThread did not produce a region");

    assert(region->kind == TraceNodeKind::REGION);
    assert(region->num_ops == NUM_OPS);
    assert(region->plan != nullptr);
    assert(region->plan->pool_bytes > 0);
    assert(region->plan->num_slots > 0);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        assert(region->ops[i].schema_hash == SCHEMA[i]);
        assert(region->ops[i].shape_hash == SHAPE[i]);
        assert(region->ops[i].output_slot_ids != nullptr);
        assert(region->ops[i].output_slot_ids[0].is_valid());
    }

    // The feed never states an edge.  Matching slots here mean the
    // planner recovered the producer-consumer edge from the shared data
    // pointer alone.
    for (uint32_t i = 1; i < NUM_OPS; i++) {
        assert(region->ops[i].input_slot_ids != nullptr);
        assert(region->ops[i].input_slot_ids[0] == region->ops[i - 1].output_slot_ids[0]);
    }

    CrucibleContext ctx;
    assert(ctx.is_recording());
    assert(ctx.activate(region));
    assert(ctx.is_compiled());
    assert(ctx.active_region() == region);
    assert(ctx.pool().is_initialized());

    auto cv = ctx.mint_compiled_view(kVigilForeground);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv);
        if (i < NUM_OPS - 1) {
            assert(s == ReplayStatus::MATCH);
        } else {
            assert(s == ReplayStatus::COMPLETE);
        }
        assert(ctx.output_ptr(0, cv) != nullptr);
    }
    assert(ctx.compiled_iterations() == 1);

    // No explicit reset: the first advance after COMPLETE rewinds.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv);
        if (i < NUM_OPS - 1) {
            assert(s == ReplayStatus::MATCH);
        } else {
            assert(s == ReplayStatus::COMPLETE);
        }
    }
    assert(ctx.compiled_iterations() == 2);
    assert(ctx.diverged_count() == 0);

    ctx.deactivate();
    assert(ctx.is_recording());

    bt.stop();
    delete meta_log;
    delete ring;

    crucible::test::pass("  test_pipeline_basic: PASSED\n");
}

void test_pipeline_divergence() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog();

    BackgroundThread bt;

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(ring, meta_log);

    wait_processed(bt, *ring);
    auto* region = bt.active_region.load(std::memory_order_acquire);
    assert(region != nullptr);

    CrucibleContext ctx;
    assert(ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    for (uint32_t i = 0; i < 3; i++) {
        assert(ctx.advance(SCHEMA[i], SHAPE[i], cv) == ReplayStatus::MATCH);
    }

    assert(ctx.advance(SchemaHash{0xBAD}, SHAPE[3], cv) == ReplayStatus::DIVERGED);
    assert(ctx.diverged_count() == 1);
    // A divergence does not switch modes.  Choosing a fallback is the
    // caller's decision, not the context's.
    assert(ctx.is_compiled());

    // The position did not move, so op 3 is still the one under test.
    // This time the schema matches and the shape does not.
    assert(ctx.advance(SCHEMA[3], ShapeHash{0xBAD}, cv) == ReplayStatus::DIVERGED);
    assert(ctx.diverged_count() == 2);

    ctx.deactivate();
    assert(ctx.is_recording());

    bt.stop();
    delete meta_log;
    delete ring;

    crucible::test::pass("  test_pipeline_divergence: PASSED\n");
}

}  // namespace test_end_to_end
