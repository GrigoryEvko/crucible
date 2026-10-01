// The divergence reset and the publish stage of
// test_background_thread_run_in_row.

#include "background_thread_run_in_row.h"

#include <crucible/BackgroundThread.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include "test_assert.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
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
// The cost is that the block has to be safe.  A callback that blocked while
// holding the arena gate would deadlock the build stage behind it, so the
// rig is also a live check that the publish path holds nothing the rest of
// the pipeline needs.
namespace publish_rig {

struct Gate {
    BackgroundThread* owner = nullptr;

    // Raised by the publish stage when it enters the callback, lowered by
    // the test when it wants the stage to continue.
    std::atomic<bool> in_callback{false};
    std::atomic<bool> release{false};
    std::atomic<bool> hold_next{true};

    // The arena-gate probe, run on the first callback only.
    std::atomic<bool> probe_done{false};
    std::atomic<bool> gate_was_free{false};

    // Set by the test once it has watched the detect stage consume the
    // reset.  Every publish after that point is a post-reset publish.
    std::atomic<bool> reset_observed{false};

    std::atomic<uint32_t> published{0};
    std::atomic<uint32_t> published_after_reset{0};
    std::atomic<uint32_t> stale_after_reset{0};
    std::atomic<uint32_t> hash_mismatches{0};

    // Which op family the region's first op came from.  Family A is
    // everything recorded before the reset.
    static constexpr uint64_t FAMILY_A = 0x00A00000ULL;
    static constexpr uint64_t FAMILY_B = 0x00B00000ULL;
    static constexpr uint64_t FAMILY_MASK = 0x00FF0000ULL;
};

static void on_region_ready(void* ctx, eff::Bg const& bg, crucible::BackgroundThread::PublishStage,
                            crucible::RegionNode* region) noexcept {
    auto* gate = static_cast<Gate*>(ctx);

    // The hash the background thread folded while streaming the ops must
    // equal the hash the canonical span fold produces over the same ops.
    // This is the assertion that binds the production producer to the
    // canonical one on a region that really came off the pipeline.
    const crucible::ContentHash canonical =
        crucible::compute_content_hash(std::span<const crucible::TraceEntry>{region->ops, region->num_ops});
    if (canonical != region->content_hash) gate->hash_mismatches.fetch_add(1, std::memory_order_relaxed);

    // The callback must not run inside the arena gate.  The gate is not
    // recursive, so a stage that still held it across this call could never
    // take it here no matter how long it waited: a successful acquire is
    // proof the publish path let go before calling out.  The deadline
    // absorbs the build stage legitimately holding the gate mid-allocation.
    if (gate->owner != nullptr && !gate->probe_done.exchange(true, std::memory_order_acq_rel)) {
        const ::fixy::BgLoadCtx probe_ctx{bg};
        const auto probe_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        bool took = false;
        while (!(took = gate->owner->try_probe_arena_alloc_gate(probe_ctx))
               && std::chrono::steady_clock::now() < probe_deadline) {
            CRUCIBLE_SPIN_PAUSE;
        }
        gate->gate_was_free.store(took, std::memory_order_release);
    }

    const bool after_reset = gate->reset_observed.load(std::memory_order_acquire);
    const uint64_t family = region->ops[0].schema_hash.raw() & Gate::FAMILY_MASK;

    gate->published.fetch_add(1, std::memory_order_release);
    if (after_reset) {
        gate->published_after_reset.fetch_add(1, std::memory_order_release);
        if (family == Gate::FAMILY_A) gate->stale_after_reset.fetch_add(1, std::memory_order_release);
    }

    if (!gate->hold_next.load(std::memory_order_acquire)) return;
    gate->hold_next.store(false, std::memory_order_release);
    gate->in_callback.store(true, std::memory_order_release);
    while (!gate->release.load(std::memory_order_acquire)) {
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
//   2. the test signals the reset and pushes family-B ops, which is what
//      wakes the detect stage so it can consume the signal
//   3. the test waits for the reset epoch to advance, which the detect
//      stage does as the last step of the reset
//   4. the callback is released, and the queued A regions drain
// A family-A region published after step 3 is the defect.
//
// Step 3 cannot wait for the flag instead.  The flag goes down when the
// detect stage takes the signal, before the reset runs, so a publish that
// the test releases then can still read the old epoch.
void test_reset_drops_inflight_regions() {
    using namespace publish_rig;

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    Gate gate;
    gate.owner = &bt;

    bt.set_region_ready_callback(&gate, &on_region_ready);
    bt.start(ring.get(), metalog.get());

    for (uint32_t iter = 0; iter < A_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(*ring, Gate::FAMILY_A, op);

    // Wait for the publish stage to be parked in the callback with at least
    // two boundaries behind it, so there is work in flight to drop.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((!gate.in_callback.load(std::memory_order_acquire) || bt.iterations_completed.get() < 3)
           && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(gate.in_callback.load(std::memory_order_acquire) && "publish stage never reached the callback");
    assert(bt.iterations_completed.get() >= 3 && "detector never cut enough work to leave any in flight");
    const uint32_t published_before_reset = gate.published.load(std::memory_order_acquire);
    assert(published_before_reset == 1 && "the gate should hold the pipeline at the first publish");

    // The foreground's divergence handler does exactly this.
    const uint32_t epoch_before_reset = bt.reset_epoch.get();
    bt.reset_requested.signal();

    // The detect stage only looks at the flag when a batch arrives, so the
    // signal needs traffic behind it.
    for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
        push(*ring, Gate::FAMILY_B, op);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (bt.reset_epoch.get() == epoch_before_reset && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.reset_epoch.get() != epoch_before_reset && "detect stage never ran the reset");
    assert(!bt.reset_requested.peek() && "the reset ran, so the detect stage took the signal first");

    // Nothing can have published while the stage was parked, so the counter
    // is still where it was and the marker below cannot race a publish.
    assert(gate.published.load(std::memory_order_acquire) == published_before_reset);
    gate.reset_observed.store(true, std::memory_order_release);

    gate.release.store(true, std::memory_order_release);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const uint64_t target = ring->total_produced();
    while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    // The commit markers have to keep flowing past a dropped region, or a
    // foreground flush() would never return.
    assert(bt.total_processed.load_acquire() >= target && "commit markers stalled behind a dropped region");

    bt.stop();

    assert(gate.hash_mismatches.load() == 0
           && "streaming content hash disagreed with the canonical span fold on a published region");

    const uint32_t stale = gate.stale_after_reset.load();
    // stderr, because the assert below aborts and stdout is block-buffered
    // into a pipe, so a printf here would be lost exactly when it matters.
    if (stale != 0)
        std::fprintf(stderr,
                     "  reset_drops_inflight_regions FAILED: %u of %u post-reset publishes carried "
                     "pre-divergence ops (cut=%u)\n",
                     stale, gate.published_after_reset.load(), bt.iterations_completed.get());
    assert(stale == 0 && "a region built from pre-divergence entries published after the reset");

    std::printf("  reset_drops_inflight_regions:              "
                "PASSED (published=%u, after reset=%u, stale=%u, cut=%u)\n",
                gate.published.load(), gate.published_after_reset.load(), stale, bt.iterations_completed.get());
}

// The region-ready callback must not run inside the arena gate.  The
// callback commits a transaction, flips the mode cell and reads sampled
// counters, and inside the gate the build stage would sleep for all of it.
//
// The probe inside the callback is what decides it, and the deadline there
// is what makes the verdict unambiguous: the gate is not recursive, so a
// publish stage still holding it could not take it again however long it
// waited.
void test_callback_runs_outside_arena_gate() {
    using namespace publish_rig;

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    Gate gate;
    gate.owner = &bt;
    // Nothing is parked here.  Holding the callback would only keep the
    // pipeline waiting on a verdict the first callback already reached.
    gate.hold_next.store(false, std::memory_order_release);

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

    assert(gate.probe_done.load() && "no region published, so the gate was never probed");
    assert(gate.gate_was_free.load() && "the region-ready callback ran while the arena gate was held");
    assert(gate.hash_mismatches.load() == 0);

    // The queue is bounded.  This run is shorter than the bound, so the
    // assertion is on the relationship rather than on a constant.
    // test_uncompiled_queue_is_bounded drives the wrap itself.
    assert(bt.uncompiled_regions.size() <= BackgroundThread::UncompiledRegionQueue::CAP);
    assert(bt.uncompiled_regions.size()
           == std::min<uint64_t>(bt.uncompiled_regions.total(), BackgroundThread::UncompiledRegionQueue::CAP));

    std::printf("  callback_outside_arena_gate:               "
                "PASSED (gate free in callback, retained=%u of %llu)\n",
                bt.uncompiled_regions.size(), static_cast<unsigned long long>(bt.uncompiled_regions.total()));
}

}  // namespace test_background_thread_run_in_row
