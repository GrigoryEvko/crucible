// The divergence reset and the publish stage of
// test_background_thread_run_in_row.

#include "background_thread_run_in_row.h"

#include <crucible/BackgroundThread.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include "test_assert.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>

using crucible::BackgroundThread;
using crucible::TraceRing;
using crucible::MetaLog;
namespace eff = ::foundation::effects;

// ── Divergence reset and the publish stage ──────────────────────────────
//
// These two share a rig, so it is described once here.
//
// A region-ready callback that blocks is the lever.  The publish stage
// cannot leave the callback until the test lets it, which pins the pipeline
// at a known point and turns two races into an ordered sequence.
//
// The cost is that the block has to be safe.  The build stage and the publish
// stage share no lock, so the build stage goes on while the callback blocks.
namespace publish_rig {

struct Gate {
    // Raised by the publish stage when it enters the callback, lowered by
    // the test when it wants the stage to continue.
    ::fixy::Atomic<bool> in_callback{false};
    ::fixy::Atomic<bool> release{false};
    ::fixy::Atomic<bool> hold_next{true};

    // Set by the test once it has watched the detect stage consume the
    // reset.  Every publish after that point is a post-reset publish.
    ::fixy::Atomic<bool> reset_observed{false};

    ::fixy::Atomic<uint32_t> published{0u};
    ::fixy::Atomic<uint32_t> published_after_reset{0u};
    ::fixy::Atomic<uint32_t> stale_after_reset{0u};
    ::fixy::Atomic<uint32_t> hash_mismatches{0u};

    // The reset point of the region that the callback holds.
    ::fixy::Atomic<uint64_t> held_reset_point{0u};

    // Which op family the region's first op came from.  Family A is
    // everything recorded before the reset.
    static constexpr uint64_t FAMILY_A = 0x00A00000ULL;
    static constexpr uint64_t FAMILY_B = 0x00B00000ULL;
    static constexpr uint64_t FAMILY_MASK = 0x00FF0000ULL;
};

static void on_region_ready(void* ctx, eff::Bg const&, crucible::BackgroundThread::PublishStage,
                            crucible::BackgroundThread::PublishedRegion const& published) noexcept {
    auto* gate = static_cast<Gate*>(ctx);
    const crucible::RegionNode* const region = published.region;

    // The hash the background thread folded while streaming the ops must
    // equal the hash the canonical span fold produces over the same ops.
    // This is the assertion that binds the production producer to the
    // canonical one on a region that really came off the pipeline.
    const crucible::ContentHash canonical =
        crucible::compute_content_hash(std::span<const crucible::TraceEntry>{region->ops, region->num_ops});
    if (canonical != region->content_hash) (void)gate->hash_mismatches.fetch_add_acq_rel(1u);

    const bool after_reset = gate->reset_observed.load_acquire();
    const uint64_t family = region->ops[0].schema_hash.raw() & Gate::FAMILY_MASK;

    (void)gate->published.fetch_add_acq_rel(1u);
    if (after_reset) {
        (void)gate->published_after_reset.fetch_add_acq_rel(1u);
        if (family == Gate::FAMILY_A) (void)gate->stale_after_reset.fetch_add_acq_rel(1u);
    }

    if (!gate->hold_next.load_acquire()) return;
    gate->hold_next.store_release(false);
    gate->held_reset_point.store_release(published.reset_point.ops_before);
    gate->in_callback.store_release(true);
    while (!gate->release.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

static void push(crucible::TraceRing& ring, uint64_t family, uint32_t op) {
    crucible::TraceRing::Entry e{};
    e.schema_hash = crucible::SchemaHash{family | op};
    e.shape_hash = crucible::ShapeHash{0x5000ULL + op};
    while (!ring.try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})
                .peek()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Enough repeats of one signature for the detector to confirm it and then
// report boundaries.  The first full re-match only confirms; boundaries
// start at the one after.
static constexpr uint32_t OPS_PER_ITER = 8;
static constexpr uint32_t A_ITERS = 6;

}  // namespace publish_rig

namespace test_background_thread_run_in_row {

// A divergence reset must drop the work the pipeline is already carrying.
//
// Without that, a region cut entirely from pre-divergence entries finishes
// its build and publishes after the reset.  The foreground has just
// deactivated replay and gone back to recording, and this publish hands it
// back the shape that diverged.
//
// The sequence the rig makes deterministic:
//   1. family-A ops flow until the first region reaches the callback, which
//      blocks there with more A regions queued behind it
//   2. the test requests the reset at the point after the last A op, and
//      pushes family-B ops
//   3. the test waits for the detect stage to apply the reset at that point
//   4. the callback is released, and the queued A regions drain
// A family-A region published after step 3 is the defect.
void test_reset_drops_inflight_regions() {
    using namespace publish_rig;

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    Gate gate;

    bt.set_region_ready_callback(&gate, &on_region_ready);
    bt.start(ring.get(), metalog.get());

    for (uint32_t iter = 0; iter < A_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(*ring, Gate::FAMILY_A, op);

    // Wait for the publish stage to be parked in the callback with at least
    // two boundaries behind it, so there is work in flight to drop.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((!gate.in_callback.load_acquire() || bt.iterations_completed.get() < 3)
           && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(gate.in_callback.load_acquire() && "publish stage never reached the callback");
    assert(bt.iterations_completed.get() >= 3 && "detector never cut enough work to leave any in flight");
    const uint32_t published_before_reset = gate.published.load_acquire();
    assert(published_before_reset == 1 && "the gate should hold the pipeline at the first publish");

    // The foreground's divergence handler does exactly this: each op in the
    // ring was recorded before the divergence.
    const BackgroundThread::StreamPoint reset_point{.ops_before = ring->total_produced()};
    bt.request_reset(reset_point);
    assert(bt.requested_reset() == reset_point);

    for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
        push(*ring, Gate::FAMILY_B, op);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (bt.applied_reset() != reset_point && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.applied_reset() == reset_point && "detect stage never applied the reset at its point");

    // Nothing can have published while the stage was parked, so the counter
    // is still where it was and the marker below cannot race a publish.
    assert(gate.published.load_acquire() == published_before_reset);
    gate.reset_observed.store_release(true);

    gate.release.store_release(true);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const uint64_t target = ring->total_produced();
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    // The commit markers have to keep flowing past a dropped region, or a
    // foreground flush() would never return.
    assert(bt.total_processed.load_acquire() >= target && "commit markers stalled behind a dropped region");

    bt.stop();

    assert(gate.hash_mismatches.load_acquire() == 0
           && "streaming content hash disagreed with the canonical span fold on a published region");

    const uint32_t stale = gate.stale_after_reset.load_acquire();
    assert(stale == 0 && "a region built from pre-divergence entries published after the reset");

    ::fixy::report(::fixy::Sink::Out,
                   "  reset_drops_inflight_regions:              "
                   "PASSED (published={}, after reset={}, stale={}, cut={})\n",
                   gate.published.load_acquire(), gate.published_after_reset.load_acquire(), stale,
                   bt.iterations_completed.get());
}

// A divergence reset applies at its point of the op stream, and not at the op
// that the detect stage reads when it first sees the request.
//
// The held callback stops the publish stage.  The backlog has more regions
// than the build channel and the publish channel hold, so the build stage and
// then the detect stage wait in the middle of the backlog.  The test then
// requests the reset at the point after the last op of the backlog.  The ops
// of the backlog after the place where the detect stage waits were recorded
// before the divergence too.  A reset that applies where the detect stage
// waits puts them into the history after the reset, and their periods make
// regions of the shape that diverged.
void test_reset_applies_at_its_point() {
    using namespace publish_rig;
    // More regions than the two channels behind the publish stage hold.
    constexpr uint32_t BACKLOG_ITERS = 400;

    const auto test = ::foundation::effects::testing::test();
    BackgroundThread bt;
    ::fixy::Box<TraceRing> ring = ::fixy::mint_box<TraceRing>(test.alloc);
    ::fixy::Box<MetaLog> metalog = ::fixy::mint_box<MetaLog>(test.alloc);
    Gate gate;

    bt.set_region_ready_callback(&gate, &on_region_ready);
    bt.start(&*ring, &*metalog);

    for (uint32_t iter = 0; iter < BACKLOG_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(*ring, Gate::FAMILY_A, op);

    while (!gate.in_callback.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }

    const BackgroundThread::StreamPoint reset_point{.ops_before = ring->total_produced()};
    bt.request_reset(reset_point);
    gate.reset_observed.store_release(true);
    assert(bt.applied_reset() != reset_point && "the detect stage waits inside the backlog");

    // The region that the callback holds passed the stale check of the
    // publish stage before the request.  Its point tells a consumer that it
    // is stale.
    assert(gate.held_reset_point.load_acquire() != reset_point.ops_before);

    for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
        push(*ring, Gate::FAMILY_B, op);
    gate.release.store_release(true);

    const uint64_t target = ring->total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.stop();

    assert(bt.applied_reset() == reset_point && "the detect stage did not apply the reset at its point");
    assert(gate.stale_after_reset.load_acquire() == 0
           && "a region of ops recorded before the divergence published after the request");
    assert(gate.published_after_reset.load_acquire() == 0 && "the ops after the point make no region");

    ::fixy::report(::fixy::Sink::Out, "  reset_applies_at_its_point:                PASSED (published={}, cut={})\n",
                   gate.published.load_acquire(), bt.iterations_completed.get());
}

// Each region that the pipeline publishes carries the content hash of the
// canonical span fold, and the uncompiled queue keeps at most its bound.
void test_published_regions_keep_their_hashes() {
    using namespace publish_rig;

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    Gate gate;
    // Nothing is parked here.  The test reads each callback, and holding one
    // would only keep the pipeline waiting.
    gate.hold_next.store_release(false);

    bt.set_region_ready_callback(&gate, &on_region_ready);
    bt.start(ring.get(), metalog.get());

    for (uint32_t iter = 0; iter < A_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(*ring, Gate::FAMILY_A, op);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const uint64_t target = ring->total_produced();
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.total_processed.load_acquire() >= target);

    bt.stop();

    assert(bt.uncompiled_regions.total() > 0 && "no region published");
    assert(gate.hash_mismatches.load_acquire() == 0);

    // The queue is bounded.  This run is shorter than the bound, so the
    // assertion is on the relationship rather than on a constant.
    // test_uncompiled_queue_is_bounded drives the wrap itself.
    assert(bt.uncompiled_regions.size() <= BackgroundThread::UncompiledRegionQueue::CAP);
    assert(bt.uncompiled_regions.size()
           == std::min<uint64_t>(bt.uncompiled_regions.total(), BackgroundThread::UncompiledRegionQueue::CAP));

    crucible::test::pass("  published_regions_keep_their_hashes:       "
                         "PASSED (retained={} of {})\n",
                         bt.uncompiled_regions.size(), static_cast<unsigned long long>(bt.uncompiled_regions.total()));
}

// A long run keeps the bytes of its scratch arenas.  The build stage ends an
// epoch of its scratch arena after each work item, and the publish stage ends
// an epoch of its own after each memory plan.  Each arena then holds the
// blocks of its largest epoch, and no more blocks for each region.  The run
// has no foreground, so the region store releases each region that neither
// the uncompiled queue nor the last publish holds.
void test_long_run_keeps_its_scratch() {
    using namespace publish_rig;
    constexpr uint32_t LONG_RUN_ITERS = 10000;

    auto test = ::foundation::effects::testing::test();
    BackgroundThread bt;
    ::fixy::Box<TraceRing> ring = ::fixy::mint_box<TraceRing>(test.alloc);
    ::fixy::Box<MetaLog> metalog = ::fixy::mint_box<MetaLog>(test.alloc);
    const size_t scratch_before = bt.scratch_retained_bytes();

    bt.start(&*ring, &*metalog);
    for (uint32_t iter = 0; iter < LONG_RUN_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(*ring, Gate::FAMILY_A, op);

    const uint64_t target = ring->total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.stop();

    const uint64_t published = bt.uncompiled_regions.total();
    assert(published >= LONG_RUN_ITERS / 2 && "the run published too few regions to witness the scratch");
    assert(bt.scratch_retained_bytes() == scratch_before && "a scratch arena kept more blocks after its epochs");

    uint32_t stored = 0;
    uint64_t dropped = 0;
    bt.run_on_publish_stage([&stored, &dropped, &bt](BackgroundThread::PublishStage const& stage) noexcept {
        stored = bt.region_count(stage);
        dropped = bt.regions_dropped_full(stage);
    });
    assert(dropped == 0 && "a full region store discarded a published region");
    assert(stored <= BackgroundThread::UncompiledRegionQueue::CAP + 1
           && "the region store kept a region that no holder names");

    crucible::test::pass("  long_run_keeps_its_scratch:                "
                         "PASSED (regions={}, stored={}, scratch={} bytes)\n",
                         static_cast<unsigned long long>(published), stored,
                         static_cast<unsigned long long>(bt.scratch_retained_bytes()));
}

}  // namespace test_background_thread_run_in_row
