// The divergence reset and the publish stage of
// test_background_thread_run_in_row.

#include "background_thread_run_in_row.h"
#include "trace_rig.h"

#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
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

    // The region callback.  A test declares its Gate and the CallRef of this
    // function before its BackgroundThread, so the two outlive the join of
    // the pipeline thread.
    void on_region_ready(eff::Bg const&, BackgroundThread::PublishStage const&,
                         BackgroundThread::PublishedRegion const& ready) noexcept;
};

void Gate::on_region_ready(eff::Bg const&, BackgroundThread::PublishStage const&,
                           BackgroundThread::PublishedRegion const& ready) noexcept {
    const crucible::DagRef<crucible::RegionNode const> region =
        crucible::ConstRegionLink{ready.region}.expect("the publish stage gives a record of a region");

    // The hash the background thread folded while streaming the ops must
    // equal the hash the canonical span fold produces over the same ops,
    // under the numerics hash of the region.  This is the assertion that
    // binds the production producer to the canonical one on a region that
    // really came off the pipeline.
    const crucible::ContentHash canonical = crucible::compute_content_hash(region->ops.view(), region->numerics);
    if (canonical != region->content_hash) (void)hash_mismatches.fetch_add_acq_rel(1u);

    const bool after_reset = reset_observed.load_acquire();
    const uint64_t family = region->op_at(0).schema_hash.raw() & FAMILY_MASK;

    (void)published.fetch_add_acq_rel(1u);
    if (after_reset) {
        (void)published_after_reset.fetch_add_acq_rel(1u);
        if (family == FAMILY_A) (void)stale_after_reset.fetch_add_acq_rel(1u);
    }

    if (!hold_next.load_acquire()) return;
    hold_next.store_release(false);
    held_reset_point.store_release(ready.reset_point.ops_before());
    in_callback.store_release(true);
    while (!release.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

static void push(crucible::test::TraceRig& rig, uint64_t family, uint32_t op) {
    crucible::TraceRing::Entry e{};
    e.schema_hash = crucible::SchemaHash{family | op};
    e.shape_hash = crucible::ShapeHash{0x5000ULL + op};
    while (!rig.append_entry(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})) {
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

    Gate gate;
    const BackgroundThread::RegionReadyCall on_ready =
        BackgroundThread::RegionReadyCall::bind<&Gate::on_region_ready>(gate);
    crucible::test::TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), on_ready};

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});

    for (uint32_t iter = 0; iter < A_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(rig, Gate::FAMILY_A, op);

    // Wait for the publish stage to be parked in the callback with at least
    // two boundaries behind it, so there is work in flight to drop.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((!gate.in_callback.load_acquire() || bt.iterations_completed.get() < 3)
           && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest, gate.in_callback.load_acquire() && "publish stage never reached the callback");
    CRUCIBLE_TEST_ASSERT(kTest,
                         bt.iterations_completed.get() >= 3 && "detector never cut enough work to leave any in flight");
    const uint32_t published_before_reset = gate.published.load_acquire();
    CRUCIBLE_TEST_ASSERT(kTest,
                         published_before_reset == 1 && "the gate should hold the pipeline at the first publish");

    // The foreground's divergence handler does exactly this: each op in the
    // ring was recorded before the divergence.
    const BackgroundThread::StreamPoint reset_point =
        BackgroundThread::StreamPoint::restart_at(rig.ring_total_produced());
    bt.request_restart(rig.ring_total_produced());
    CRUCIBLE_TEST_ASSERT(kTest, bt.requested_reset() == reset_point);

    for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
        push(rig, Gate::FAMILY_B, op);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (bt.applied_reset() != reset_point && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest,
                         bt.applied_reset() == reset_point && "detect stage never applied the reset at its point");

    // Nothing can have published while the stage was parked, so the counter
    // is still where it was and the marker below cannot race a publish.
    CRUCIBLE_TEST_ASSERT(kTest, gate.published.load_acquire() == published_before_reset);
    gate.reset_observed.store_release(true);

    gate.release.store_release(true);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    // The commit markers have to keep flowing past a dropped region, or a
    // foreground flush() would never return.
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= target
                                    && "commit markers stalled behind a dropped region");

    bt.stop();

    CRUCIBLE_TEST_ASSERT(kTest,
                         gate.hash_mismatches.load_acquire() == 0
                             && "streaming content hash disagreed with the canonical span fold on a published region");

    const uint32_t stale = gate.stale_after_reset.load_acquire();
    CRUCIBLE_TEST_ASSERT(kTest, stale == 0 && "a region built from pre-divergence entries published after the reset");

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

    Gate gate;
    const BackgroundThread::RegionReadyCall on_ready =
        BackgroundThread::RegionReadyCall::bind<&Gate::on_region_ready>(gate);
    crucible::test::TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), on_ready};

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});

    for (uint32_t iter = 0; iter < BACKLOG_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(rig, Gate::FAMILY_A, op);

    while (!gate.in_callback.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }

    const BackgroundThread::StreamPoint reset_point =
        BackgroundThread::StreamPoint::restart_at(rig.ring_total_produced());
    bt.request_restart(rig.ring_total_produced());
    gate.reset_observed.store_release(true);
    CRUCIBLE_TEST_ASSERT(kTest, bt.applied_reset() != reset_point && "the detect stage waits inside the backlog");

    // The region that the callback holds passed the stale check of the
    // publish stage before the request.  Its point tells a consumer that it
    // is stale.
    CRUCIBLE_TEST_ASSERT(kTest, gate.held_reset_point.load_acquire() != reset_point.ops_before());

    for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
        push(rig, Gate::FAMILY_B, op);
    gate.release.store_release(true);

    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.stop();

    CRUCIBLE_TEST_ASSERT(kTest,
                         bt.applied_reset() == reset_point && "the detect stage did not apply the reset at its point");
    CRUCIBLE_TEST_ASSERT(kTest, gate.stale_after_reset.load_acquire() == 0
                                    && "a region of ops recorded before the divergence published after the request");
    CRUCIBLE_TEST_ASSERT(kTest,
                         gate.published_after_reset.load_acquire() == 0 && "the ops after the point make no region");

    ::fixy::report(::fixy::Sink::Out, "  reset_applies_at_its_point:                PASSED (published={}, cut={})\n",
                   gate.published.load_acquire(), bt.iterations_completed.get());
}

// A mark that comes before the detect stage reaches an older restart is a
// restart at the point of the mark.  A mark that replaced the restart would
// leave the ops of the divergence in the detector.  A mark after an applied
// reset stays a mark.  The held callback keeps the detect stage inside the
// backlog, so it cannot apply the restart before the mark.
void test_mark_keeps_an_unapplied_restart() {
    using namespace publish_rig;
    constexpr uint32_t BACKLOG_ITERS = 400;
    using StreamPoint = BackgroundThread::StreamPoint;

    Gate gate;
    const BackgroundThread::RegionReadyCall on_ready =
        BackgroundThread::RegionReadyCall::bind<&Gate::on_region_ready>(gate);
    crucible::test::TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), on_ready};

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});

    for (uint32_t iter = 0; iter < BACKLOG_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(rig, Gate::FAMILY_A, op);
    while (!gate.in_callback.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }

    const uint64_t restart_ops = rig.ring_total_produced();
    bt.request_restart(restart_ops);
    CRUCIBLE_TEST_ASSERT(kTest, bt.applied_reset() != StreamPoint::restart_at(restart_ops)
                                    && "the detect stage waits inside the backlog");
    for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
        push(rig, Gate::FAMILY_B, op);
    const uint64_t mark_ops = rig.ring_total_produced();
    bt.request_mark(mark_ops);
    CRUCIBLE_TEST_ASSERT(kTest, bt.requested_reset() == StreamPoint::restart_at(mark_ops)
                                    && "a mark lost an unapplied restart");

    gate.release.store_release(true);
    uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest, bt.applied_reset() == StreamPoint::restart_at(mark_ops));

    push(rig, Gate::FAMILY_B, 0);
    const uint64_t later_mark_ops = rig.ring_total_produced();
    bt.request_mark(later_mark_ops);
    CRUCIBLE_TEST_ASSERT(kTest, bt.requested_reset() == StreamPoint::mark_at(later_mark_ops)
                                    && "a mark after an applied reset changed");
    push(rig, Gate::FAMILY_B, 0);
    target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.stop();
    CRUCIBLE_TEST_ASSERT(kTest, bt.applied_reset() == StreamPoint::mark_at(later_mark_ops)
                                    && "the detect stage did not apply the mark");

    ::fixy::report(::fixy::Sink::Out, "  mark_keeps_an_unapplied_restart:           PASSED\n");
}

// The build stage builds no work of a backlog that a reset made stale.
//
// The held callback stops the publish stage, so the build stage builds at
// most the works that the channel to the publish stage holds and the one in
// its hand.  The reset then comes at the point after the backlog.  Each
// later work of the backlog was cut before that point.
void test_reset_skips_the_stale_backlog() {
    using namespace publish_rig;
    constexpr uint32_t BACKLOG_ITERS = 400;

    Gate gate;
    const BackgroundThread::RegionReadyCall on_ready =
        BackgroundThread::RegionReadyCall::bind<&Gate::on_region_ready>(gate);
    crucible::test::TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), on_ready};

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});

    for (uint32_t iter = 0; iter < BACKLOG_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(rig, Gate::FAMILY_A, op);
    while (!gate.in_callback.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.request_restart(rig.ring_total_produced());
    gate.release.store_release(true);

    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.stop();

    CRUCIBLE_TEST_ASSERT(kTest, bt.iterations_completed.get() > BACKLOG_ITERS / 2
                                    && "the detect stage cut each work of the backlog");
    CRUCIBLE_TEST_ASSERT(kTest,
                         bt.works_built() < BACKLOG_ITERS / 2 && "the build stage built the works of a stale backlog");

    ::fixy::report(::fixy::Sink::Out, "  reset_skips_the_stale_backlog:             PASSED (cut={}, built={})\n",
                   bt.iterations_completed.get(), bt.works_built());
}

// Each region that the pipeline publishes carries the content hash of the
// canonical span fold, and the uncompiled queue keeps at most its bound.
void test_published_regions_keep_their_hashes() {
    using namespace publish_rig;

    Gate gate;
    // Nothing is parked here.  The test reads each callback, and holding one
    // would only keep the pipeline waiting.
    gate.hold_next.store_release(false);
    const BackgroundThread::RegionReadyCall on_ready =
        BackgroundThread::RegionReadyCall::bind<&Gate::on_region_ready>(gate);
    crucible::test::TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), on_ready};

    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});

    for (uint32_t iter = 0; iter < A_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(rig, Gate::FAMILY_A, op);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= target);

    bt.stop();

    CRUCIBLE_TEST_ASSERT(kTest, bt.uncompiled_regions().total() > 0 && "no region published");
    CRUCIBLE_TEST_ASSERT(kTest, gate.hash_mismatches.load_acquire() == 0);

    // The queue is bounded.  This run is shorter than the bound, so the
    // assertion is on the relationship rather than on a constant.
    // test_uncompiled_queue_is_bounded drives the wrap itself.
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    CRUCIBLE_TEST_ASSERT(test, bt.uncompiled_regions().size() <= bt.uncompiled_regions().capacity());
    CRUCIBLE_TEST_ASSERT(
        test, bt.uncompiled_regions().size()
                  == ::fixy::min(bt.uncompiled_regions().total(), uint64_t{bt.uncompiled_regions().capacity()}));

    ::fixy::check::pass(kTest,
                        "  published_regions_keep_their_hashes:       "
                        "PASSED (retained={} of {})\n",
                        bt.uncompiled_regions().size(),
                        static_cast<unsigned long long>(bt.uncompiled_regions().total()));
}

// Pushes iters iterations of family A through bt, waits until the pipeline
// processed each op, and stops bt.  Gives the count of published regions.
static uint64_t run_family_a(BackgroundThread& bt, crucible::test::TraceRig& rig, uint32_t iters) {
    using namespace publish_rig;
    bt.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    for (uint32_t iter = 0; iter < iters; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(rig, Gate::FAMILY_A, op);

    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.stop();
    return bt.uncompiled_regions().total();
}

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

// The bytes of blocks that the two scratch arenas of a stopped pipeline hold.
static Bytes scratch_retained_bytes(BackgroundThread& bt) {
    Bytes publish_bytes{size_t{0}};
    bt.run_on_publish_stage([&publish_bytes](BackgroundThread::PublishStageView& owner,
                                             BackgroundThread::PublishStage const& stage) noexcept {
        publish_bytes = owner.publish_scratch_retained_bytes(stage);
    });
    return ::fixy::saturating_add(bt.build_scratch_retained_bytes(bt.prove_quiescence()), publish_bytes);
}

// A long run keeps the bytes of its scratch arenas.  The build stage ends an
// epoch of its scratch arena after each work item, and the publish stage ends
// an epoch of its own after each memory plan.  Each arena then holds the
// blocks of its largest epoch, and no more blocks for each region.  A short
// run of the same trace has the same largest epoch, so the two runs hold the
// same bytes.  The run has no foreground, so the region store releases each
// region that neither the uncompiled queue nor the last publish holds.
void test_long_run_keeps_its_scratch() {
    using namespace publish_rig;
    constexpr uint32_t LONG_RUN_ITERS = 10000;

    crucible::test::TraceRig short_rig{};
    BackgroundThread short_bt{::foundation::effects::testing::test().alloc, short_rig.ring_side(),
                              short_rig.log_side()};
    const uint64_t short_published = run_family_a(short_bt, short_rig, A_ITERS);
    CRUCIBLE_TEST_ASSERT(kTest,
                         short_published >= 1 && "the short run published no region, so its arenas witness nothing");

    crucible::test::TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side()};
    const uint64_t published = run_family_a(bt, rig, LONG_RUN_ITERS);
    CRUCIBLE_TEST_ASSERT(kTest,
                         published >= LONG_RUN_ITERS / 2 && "the run published too few regions to witness the scratch");
    const Bytes scratch_bytes = scratch_retained_bytes(bt);
    CRUCIBLE_TEST_ASSERT(kTest, scratch_bytes == scratch_retained_bytes(short_bt)
                                    && "a scratch arena kept more blocks after its epochs");

    uint32_t stored = 0;
    uint64_t dropped = 0;
    bt.run_on_publish_stage([&stored, &dropped](BackgroundThread::PublishStageView& owner,
                                                BackgroundThread::PublishStage const& stage) noexcept {
        stored = owner.region_count(stage);
        dropped = owner.regions_dropped_full(stage);
    });
    CRUCIBLE_TEST_ASSERT(kTest, dropped == 0 && "a full region store discarded a published region");
    CRUCIBLE_TEST_ASSERT(::fixy::TestRunnerCtx{::foundation::effects::testing::test()},
                         stored <= bt.uncompiled_regions().capacity() + 1);

    ::fixy::check::pass(kTest,
                        "  long_run_keeps_its_scratch:                "
                        "PASSED (regions={}, stored={}, scratch={} bytes)\n",
                        static_cast<unsigned long long>(published), stored,
                        static_cast<unsigned long long>(scratch_bytes.raw()));
}

// The limits of a pipeline refuse what passes them.  The uncompiled queue
// holds each region that the store holds, so a store of two regions holds the
// first two, and it discards each later region and counts it.  Each scratch
// arena takes the retained limit of the limits, and keeps only its first
// block after an epoch above it (test_core_bump_arena pins the refusal).
void test_limits_refuse_what_passes_them(::fixy::TestRunnerCtx const& test) {
    using namespace publish_rig;
    constexpr uint32_t ITERS = 64;
    auto witness = ::foundation::effects::testing::test();
    crucible::PipelineLimits limits{};
    limits.region_store = ::fixy::Quantity<::fixy::unit::Element, uint32_t>{2};
    limits.scratch_retained = ::fixy::Quantity<::fixy::unit::Byte, size_t>{4096};
    crucible::test::TraceRig rig{};
    BackgroundThread bt{witness.alloc, rig.ring_side(), rig.log_side(), limits};
    CRUCIBLE_TEST_ASSERT(test, bt.scratch_retained_limit() == limits.scratch_retained);
    CRUCIBLE_TEST_ASSERT(test,
                         bt.uncompiled_regions().capacity() == crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE);

    const uint64_t queued = run_family_a(bt, rig, ITERS);

    uint32_t stored = 0;
    uint64_t dropped = 0;
    bt.run_on_publish_stage([&stored, &dropped](BackgroundThread::PublishStageView& owner,
                                                BackgroundThread::PublishStage const& stage) noexcept {
        stored = owner.region_count(stage);
        dropped = owner.regions_dropped_full(stage);
    });
    CRUCIBLE_TEST_ASSERT(test, stored == 2);
    CRUCIBLE_TEST_ASSERT(test, queued == 2);
    CRUCIBLE_TEST_ASSERT(test, dropped > 0);
    CRUCIBLE_TEST_ASSERT(test, scratch_retained_bytes(bt).raw() <= size_t{2} << 20);
    ::fixy::check::pass(test, "  limits_refuse_what_passes_them:            PASSED (stored={}, dropped={})\n", stored,
                        static_cast<unsigned long long>(dropped));
}

}  // namespace test_background_thread_run_in_row
