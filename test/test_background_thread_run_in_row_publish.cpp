// The divergence reset and the publish stage of
// test_background_thread_run_in_row.

#include "background_thread_run_in_row.h"
#include "trace_rig.h"

#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <chrono>
#include <cstdint>
#include <utility>

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

    // The region callback.  run_gated declares the Gate and the CallRef of this
    // function before the frame of the thread, so the two outlive the join of
    // the pipeline thread.
    void on_region_ready(eff::Bg const&, BackgroundThread::PublishStage const&,
                         crucible::PublishedRegion const& ready) noexcept;
};

void Gate::on_region_ready(eff::Bg const&, BackgroundThread::PublishStage const&,
                           crucible::PublishedRegion const& ready) noexcept {
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
// More regions than the two channels behind the publish stage hold.
static constexpr uint32_t BACKLOG_ITERS = 400;

// Pushes the ops of the iterations of family A.
static void push_family_a(crucible::test::TraceRig& rig, uint32_t iters) {
    for (uint32_t iter = 0; iter < iters; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(rig, Gate::FAMILY_A, op);
}

// What the callback of a gate does with the first region.
enum class FirstRegion : uint8_t {
    held,
    published,
};

// Starts the pipeline of a thread whose region callback is the gate, and calls
// the check with the gate, the rig, the thread and the Running handle.
template <class Check>
void run_gated(FirstRegion first, Check check) {
    Gate gate;
    gate.hold_next.store_release(first == FirstRegion::held);
    const BackgroundThread::RegionReadyCall on_ready =
        BackgroundThread::RegionReadyCall::bind<&Gate::on_region_ready>(gate);
    crucible::test::TraceRig rig{};
    crucible::with_background_thread(eff::testing::test().alloc, rig.ring_side(), rig.log_side(),
                                     {.on_region_ready = ::fixy::some(::fixy::erase_brand(::fixy::to(on_ready)))},
                                     [&gate, &rig, &check](BackgroundThread const& bt, auto stopped) noexcept {
                                         check(gate, rig, bt,
                                               std::move(stopped).start(::fixy::InitLoadCtx{eff::testing::init()}));
                                     });
}

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
template <class Running>
static void check_reset_drops_inflight_regions(publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                               BackgroundThread const& bt, Running running) noexcept {
    using namespace publish_rig;
    push_family_a(rig, A_ITERS);

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
    const crucible::StreamPoint reset_point = crucible::StreamPoint::restart_at(rig.ring_total_produced());
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

    static_cast<void>(std::move(running).stop());

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

void test_reset_drops_inflight_regions() {
    publish_rig::run_gated(publish_rig::FirstRegion::held, [](publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                                              BackgroundThread const& bt, auto running) noexcept {
        check_reset_drops_inflight_regions(gate, rig, bt, std::move(running));
    });
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
template <class Running>
static void check_reset_applies_at_its_point(publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                             BackgroundThread const& bt, Running running) noexcept {
    using namespace publish_rig;
    push_family_a(rig, BACKLOG_ITERS);

    while (!gate.in_callback.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }

    const crucible::StreamPoint reset_point = crucible::StreamPoint::restart_at(rig.ring_total_produced());
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
    static_cast<void>(std::move(running).stop());

    CRUCIBLE_TEST_ASSERT(kTest,
                         bt.applied_reset() == reset_point && "the detect stage did not apply the reset at its point");
    CRUCIBLE_TEST_ASSERT(kTest, gate.stale_after_reset.load_acquire() == 0
                                    && "a region of ops recorded before the divergence published after the request");
    CRUCIBLE_TEST_ASSERT(kTest,
                         gate.published_after_reset.load_acquire() == 0 && "the ops after the point make no region");

    ::fixy::report(::fixy::Sink::Out, "  reset_applies_at_its_point:                PASSED (published={}, cut={})\n",
                   gate.published.load_acquire(), bt.iterations_completed.get());
}

void test_reset_applies_at_its_point() {
    publish_rig::run_gated(publish_rig::FirstRegion::held, [](publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                                              BackgroundThread const& bt, auto running) noexcept {
        check_reset_applies_at_its_point(gate, rig, bt, std::move(running));
    });
}

// A mark that comes before the detect stage reaches an older restart is a
// restart at the point of the mark.  A mark that replaced the restart would
// leave the ops of the divergence in the detector.  A mark after an applied
// reset stays a mark.  The held callback keeps the detect stage inside the
// backlog, so it cannot apply the restart before the mark.
template <class Running>
static void check_mark_keeps_an_unapplied_restart(publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                                  BackgroundThread const& bt, Running running) noexcept {
    using namespace publish_rig;
    using crucible::StreamPoint;
    push_family_a(rig, BACKLOG_ITERS);
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
    static_cast<void>(std::move(running).stop());
    CRUCIBLE_TEST_ASSERT(kTest, bt.applied_reset() == StreamPoint::mark_at(later_mark_ops)
                                    && "the detect stage did not apply the mark");

    ::fixy::report(::fixy::Sink::Out, "  mark_keeps_an_unapplied_restart:           PASSED\n");
}

void test_mark_keeps_an_unapplied_restart() {
    publish_rig::run_gated(publish_rig::FirstRegion::held, [](publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                                              BackgroundThread const& bt, auto running) noexcept {
        check_mark_keeps_an_unapplied_restart(gate, rig, bt, std::move(running));
    });
}

// The build stage builds no work of a backlog that a reset made stale.
//
// The held callback stops the publish stage, so the build stage builds at
// most the works that the channel to the publish stage holds and the one in
// its hand.  The reset then comes at the point after the backlog.  Each
// later work of the backlog was cut before that point.
template <class Running>
static void check_reset_skips_the_stale_backlog(publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                                BackgroundThread const& bt, Running running) noexcept {
    using namespace publish_rig;
    push_family_a(rig, BACKLOG_ITERS);
    while (!gate.in_callback.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }
    bt.request_restart(rig.ring_total_produced());
    gate.release.store_release(true);

    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target) {
        CRUCIBLE_SPIN_PAUSE;
    }
    static_cast<void>(std::move(running).stop());

    CRUCIBLE_TEST_ASSERT(kTest, bt.iterations_completed.get() > BACKLOG_ITERS / 2
                                    && "the detect stage cut each work of the backlog");
    CRUCIBLE_TEST_ASSERT(kTest,
                         bt.works_built() < BACKLOG_ITERS / 2 && "the build stage built the works of a stale backlog");

    ::fixy::report(::fixy::Sink::Out, "  reset_skips_the_stale_backlog:             PASSED (cut={}, built={})\n",
                   bt.iterations_completed.get(), bt.works_built());
}

void test_reset_skips_the_stale_backlog() {
    publish_rig::run_gated(publish_rig::FirstRegion::held, [](publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                                              BackgroundThread const& bt, auto running) noexcept {
        check_reset_skips_the_stale_backlog(gate, rig, bt, std::move(running));
    });
}

// Each region that the pipeline publishes carries the content hash of the
// canonical span fold, and the uncompiled queue keeps at most its bound.
template <class Running>
static void check_published_regions_keep_their_hashes(publish_rig::Gate& gate, crucible::test::TraceRig& rig,
                                                      BackgroundThread const& bt, Running running) noexcept {
    using namespace publish_rig;
    push_family_a(rig, A_ITERS);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const uint64_t target = rig.ring_total_produced();
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= target);

    static_cast<void>(std::move(running).stop());

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

// Nothing is parked here.  The test reads each callback, and holding one would
// only keep the pipeline waiting.
void test_published_regions_keep_their_hashes() {
    publish_rig::run_gated(
        publish_rig::FirstRegion::published,
        [](publish_rig::Gate& gate, crucible::test::TraceRig& rig, BackgroundThread const& bt, auto running) noexcept {
            check_published_regions_keep_their_hashes(gate, rig, bt, std::move(running));
        });
}

// What a run of family A left: the count of published regions, the capacity of
// the uncompiled queue, the retained limit, the bytes of blocks that the two
// scratch arenas hold, and the counts of the region store.
struct FamilyRun final {
    uint64_t published = 0;
    uint32_t queue_capacity = 0;
    ::fixy::Quantity<::fixy::unit::Byte, size_t> scratch_limit{size_t{0}};
    ::fixy::Quantity<::fixy::unit::Byte, size_t> scratch{size_t{0}};
    uint32_t stored = 0;
    uint64_t dropped = 0;
};

// Pushes iters iterations of family A through a pipeline with the limits, waits
// until the pipeline processed each op, and stops it.
static FamilyRun run_family_a(crucible::PipelineLimits const& limits, uint32_t iters) {
    crucible::test::TraceRig rig{};
    return crucible::with_background_thread(
        eff::testing::test().alloc, rig.ring_side(), rig.log_side(), {.limits = limits},
        [&rig, iters](BackgroundThread const& bt, auto stopped) noexcept {
            auto running = std::move(stopped).start(::fixy::InitLoadCtx{eff::testing::init()});
            publish_rig::push_family_a(rig, iters);
            const uint64_t target = rig.ring_total_produced();
            while (bt.total_processed.load_acquire() < target) {
                CRUCIBLE_SPIN_PAUSE;
            }
            auto stopped_again = std::move(running).stop();

            FamilyRun run{.published = bt.uncompiled_regions().total(),
                          .queue_capacity = bt.uncompiled_regions().capacity(),
                          .scratch_limit = bt.scratch_retained_limit(),
                          .scratch = stopped_again.build_scratch_retained_bytes()};
            stopped_again.run_on_publish_stage([&run](BackgroundThread::PublishStageView& owner,
                                                      BackgroundThread::PublishStage const& stage) noexcept {
                run.scratch = ::fixy::saturating_add(run.scratch, owner.publish_scratch_retained_bytes(stage));
                run.stored = owner.region_count(stage);
                run.dropped = owner.regions_dropped_full(stage);
            });
            return run;
        });
}

// A long run keeps the bytes of its scratch arenas.  The build stage ends an
// epoch of its scratch arena after each work item, and the publish stage ends
// an epoch of its own after each memory plan.  Each arena then holds the
// blocks of its largest epoch, and no more blocks for each region.  A short
// run of the same trace has the same largest epoch, so the two runs hold the
// same bytes.  The run has no foreground, so the region store releases each
// region that neither the uncompiled queue nor the last publish holds.
void test_long_run_keeps_its_scratch() {
    constexpr uint32_t LONG_RUN_ITERS = 10000;

    const FamilyRun short_run = run_family_a(crucible::PipelineLimits{}, publish_rig::A_ITERS);
    CRUCIBLE_TEST_ASSERT(kTest, short_run.published >= 1
                                    && "the short run published no region, so its arenas witness nothing");

    const FamilyRun run = run_family_a(crucible::PipelineLimits{}, LONG_RUN_ITERS);
    CRUCIBLE_TEST_ASSERT(kTest, run.published >= LONG_RUN_ITERS / 2
                                    && "the run published too few regions to witness the scratch");
    CRUCIBLE_TEST_ASSERT(kTest,
                         run.scratch == short_run.scratch && "a scratch arena kept more blocks after its epochs");
    CRUCIBLE_TEST_ASSERT(kTest, run.dropped == 0 && "a full region store discarded a published region");
    CRUCIBLE_TEST_ASSERT(kTest, run.stored == run.queue_capacity
                                    && "the store keeps the regions of the queue, the last one included");

    ::fixy::check::pass(kTest,
                        "  long_run_keeps_its_scratch:                "
                        "PASSED (regions={}, stored={}, scratch={} bytes)\n",
                        static_cast<unsigned long long>(run.published), run.stored,
                        static_cast<unsigned long long>(run.scratch.raw()));
}

// The limits of a pipeline refuse what passes them.  The uncompiled queue
// holds each region that the store holds, so a store of two regions holds the
// first two, and it discards each later region and counts it.  Each scratch
// arena takes the retained limit of the limits, and keeps only its first
// block after an epoch above it (test_core_bump_arena pins the refusal).
void test_limits_refuse_what_passes_them(::fixy::TestRunnerCtx const& test) {
    constexpr uint32_t ITERS = 64;
    crucible::PipelineLimits limits{};
    limits.region_store = ::fixy::Quantity<::fixy::unit::Element, uint32_t>{2};
    limits.scratch_retained = ::fixy::Quantity<::fixy::unit::Byte, size_t>{4096};
    const FamilyRun run = run_family_a(limits, ITERS);
    CRUCIBLE_TEST_ASSERT(test, run.scratch_limit == limits.scratch_retained);
    CRUCIBLE_TEST_ASSERT(test, run.queue_capacity == crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE);
    CRUCIBLE_TEST_ASSERT(test, run.stored == 2);
    CRUCIBLE_TEST_ASSERT(test, run.published == 2);
    CRUCIBLE_TEST_ASSERT(test, run.dropped > 0);
    CRUCIBLE_TEST_ASSERT(test, run.scratch.raw() <= size_t{2} << 20);
    ::fixy::check::pass(test, "  limits_refuse_what_passes_them:            PASSED (stored={}, dropped={})\n",
                        run.stored, static_cast<unsigned long long>(run.dropped));
}

}  // namespace test_background_thread_run_in_row
