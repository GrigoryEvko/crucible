// The background drain loop exhibits four effect atoms: the background
// context tag itself, arena allocation while building a region, output
// through the region-ready callback, and a wait on the arena gate.  A caller
// therefore hands in a context whose row admits those four.
//
// The context is the evidence.  The pipeline thread takes it from the door
// of the background context, and a test takes it from the test witness.
// This file holds the accepting side.  The rejecting side needs a compile to
// fail, so it lives with the negative fixtures.

#include <crucible/BackgroundThread.h>
#include <crucible/Cipher.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include "test_assert.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <span>
#include <thread>
#include <type_traits>

using crucible::BackgroundThread;
using crucible::TraceRing;
using crucible::MetaLog;
namespace eff = ::foundation::effects;

namespace {

// The background load context, over the source the test witness hands out.
[[nodiscard]] ::fixy::BgLoadCtx bg_load_ctx() noexcept { return ::fixy::BgLoadCtx{eff::testing::bg()}; }

}  // namespace

static void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<BackgroundThread::run_required_row,
                                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>,
                  "BackgroundThread::run_required_row is exactly Row<Bg, Alloc, IO, Block>.");

    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Bg>);
    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Alloc>);
    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::IO>);
    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Block>);

    // Init and Test tag other entry points.  The drain loop exhibits neither.
    static_assert(!eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Init>);
    static_assert(!eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Test>);

    static_assert(eff::row_size_v<BackgroundThread::run_required_row> == 4);

    std::printf("  T01 required_row_pinned:                   PASSED\n");
}

// The one context that admits the row, and the contexts that do not.  These
// check the predicate.  Rejecting an actual call needs a substitution
// failure, which only a fixture that fails to compile can show.
static void test_t02_context_matrix() {
    using Required = BackgroundThread::run_required_row;

    static_assert(eff::CtxAdmits<::fixy::BgLoadCtx, Required>);

    // Each background row below lacks one required atom or more.
    static_assert(!eff::CtxAdmits<::fixy::BgDrainCtx, Required>);  // lacks IO and Block
    static_assert(!eff::CtxAdmits<::fixy::BgCompileCtx, Required>);  // lacks Block
    static_assert(
        !eff::CtxAdmits<eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>,
                        Required>);  // lacks Bg
    static_assert(!eff::CtxAdmits<eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block>>,
                                  Required>);  // lacks Alloc

    // A start-up, a test and a foreground context each lack Bg.
    static_assert(!eff::CtxAdmits<::fixy::InitLoadCtx, Required>);
    static_assert(!eff::CtxAdmits<::fixy::TestRunnerCtx, Required>);
    static_assert(!eff::CtxAdmits<::fixy::HotFgCtx, Required>);

    // A value that is not a context admits nothing.
    static_assert(!eff::CtxAdmits<int, Required>);

    std::printf("  T02 context_matrix:                        PASSED\n");
}

// The pointer is never dereferenced.  The call expression sits inside
// decltype, where it is unevaluated, and only its type is taken.
static void test_t03_api_surface_pinned() {
    BackgroundThread* bt_ptr = nullptr;
    using ReturnT = decltype(bt_ptr->run_in_row(std::declval<::fixy::BgLoadCtx const&>()));
    static_assert(std::is_same_v<ReturnT, void>, "run_in_row(ctx) returns void, matching run(ctx).");
    static_assert(noexcept(bt_ptr->run_in_row(std::declval<::fixy::BgLoadCtx const&>())),
                  "run_in_row(ctx) is noexcept, matching run(ctx).");

    std::printf("  T03 api_surface_pinned:                    PASSED\n");
}

// Stop is signalled before the loop starts, so the loop body never runs and
// the call falls straight through to the trailing drain, which is empty.
// What is under test is that the entry point forwards at all.
static void test_t04_runtime_smoke() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();

    bt.ring.set(ring.get());
    bt.meta_log.set(metalog.get());
    bt.stop_requested.signal();

    bool done = false;
    std::jthread t([&]() {
        bt.run_in_row(bg_load_ctx());
        done = true;
    });
    t.join();
    assert(done);

    std::printf("  T04 runtime_smoke:                         PASSED\n");
}

// The drain loop calls record_event while committing a region, so by
// transitivity its row has to contain record_event's row.  Narrowing the
// drain row would otherwise leave that downstream call unsatisfiable, and
// this inclusion fires before any caller reaches the violation itself.
static void test_cross_fence_consistency() {
    using BgRow = BackgroundThread::run_required_row;
    using RecordRow = ::crucible::Cipher::record_event_required_row;

    static_assert(eff::Subrow<RecordRow, BgRow>, "BackgroundThread::run_required_row contains "
                                                 "Cipher::record_event_required_row. The background drain calls "
                                                 "record_event while committing a region, so a drain row that does "
                                                 "not admit IO and Block leaves that call site unsatisfiable.");

    // Containment runs one way only.  The drain row additionally admits Bg
    // and Alloc, so the two rows are not equal.
    static_assert(!eff::Subrow<BgRow, RecordRow>, "The background drain row strictly contains the record_event row: "
                                                  "Bg and Alloc are extra.");
    static_assert(eff::row_size_v<BgRow> == 4u);
    static_assert(eff::row_size_v<RecordRow> == 2u);

    std::printf("  cross_fence_consistency:                   PASSED\n");
}

// A real drain, with one thread pushing while the entry point consumes.
// What it establishes is that the entry point forwards rather than
// reimplements, so the queue's acquire and release discipline is untouched
// and the trailing drain does not hang.
//
// Nothing here builds a region.  The entries carry no tensor metadata, so the
// iteration detector advances but never closes anything, which leaves plain
// drain motion to observe.
static void test_concurrent_spsc_drain() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(ring.get());
    bt.meta_log.set(metalog.get());
    bt.stop_requested.reset_in_quiescent_context(::fixy::handle::OneShotFlag::QuiescenceProof{});

    std::jthread bg_thread([&]() { bt.run_in_row(bg_load_ctx()); });

    // Each entry carries its own schema hash so the iteration detector never
    // matches a signature.
    constexpr uint32_t N = 8;
    for (uint32_t i = 0; i < N; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0x1000ULL + i};
        e.shape_hash = crucible::ShapeHash{0x2000ULL + i};
        // The push side has a fence of its own, which is not what is under
        // test here, so this goes through the untyped append.  That append
        // returns a wrapped bool, which peek unwraps for the predicate.
        while (
            !ring->try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})
                 .peek()) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // The processed counter reaching N is what says the drain consumed every
    // entry.  The deadline keeps a stalled drain from hanging the suite.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bt.total_processed.load_acquire() < N && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.total_processed.load_acquire() >= N);

    bt.stop_requested.signal();
    bg_thread.join();

    std::printf("  concurrent_spsc_drain:                     PASSED\n");
}

// A batch large enough to span several drain batches, to catch a loop that
// stops before consuming everything it drained.
//
// The hashes are all distinct so the iteration detector never fires.  Taking
// the boundary path would need the global schema and kernel tables sealed,
// which the normal start sequence does and this test deliberately bypasses in
// order to reach the entry point directly.
static void test_large_batch_drain() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(ring.get());
    bt.meta_log.set(metalog.get());
    bt.stop_requested.reset_in_quiescent_context(::fixy::handle::OneShotFlag::QuiescenceProof{});

    std::jthread bg_thread([&]() { bt.run_in_row(bg_load_ctx()); });

    constexpr uint32_t TOTAL = 32;
    for (uint32_t i = 0; i < TOTAL; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0xABCD0001ULL + i};
        e.shape_hash = crucible::ShapeHash{0xDEAD0001ULL + i};
        while (
            !ring->try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})
                 .peek()) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bt.total_processed.load_acquire() < TOTAL && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    const uint64_t processed_after_push = bt.total_processed.load_acquire();
    assert(processed_after_push >= TOTAL);

    bt.stop_requested.signal();
    bg_thread.join();

    // No signature ever matched twice, so the boundary path never ran and the
    // completed count stays at zero.
    assert(bt.iterations_completed.get() == 0u);

    std::printf("  large_batch_drain:                         "
                "PASSED (processed=%llu of %u)\n",
                static_cast<unsigned long long>(processed_after_push), TOTAL);
}

// Spawn, stop, spawn again.  A once-flag, a static guard inside the template
// instantiation, or a destructor that failed to run would leave the second
// invocation dead, and the second cycle would then drain nothing.
static void test_rearm_cycle() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(ring.get());
    bt.meta_log.set(metalog.get());

    constexpr uint32_t PER_CYCLE = 4;

    uint64_t prev_processed = 0;

    for (uint32_t cycle = 0; cycle < 2; ++cycle) {
        bt.stop_requested.reset_in_quiescent_context(::fixy::handle::OneShotFlag::QuiescenceProof{});

        std::jthread bg([&]() { bt.run_in_row(bg_load_ctx()); });

        for (uint32_t i = 0; i < PER_CYCLE; ++i) {
            crucible::TraceRing::Entry e{};
            // The cycle index enters the hash so the detector cannot tie the
            // two cycles together into one signature.
            e.schema_hash = crucible::SchemaHash{0x10000ULL + (cycle << 16) + i};
            e.shape_hash = crucible::ShapeHash{0x20000ULL + i};
            while (!ring->try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0},
                                            crucible::CallsiteHash{0})
                        .peek()) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }

        const uint64_t target = prev_processed + PER_CYCLE;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
            CRUCIBLE_SPIN_PAUSE;
        }
        assert(bt.total_processed.load_acquire() >= target);

        bt.stop_requested.signal();
        bg.join();

        prev_processed = bt.total_processed.load_acquire();
    }

    // Both invocations consumed entries, so the total covers both cycles.
    assert(bt.total_processed.load_acquire() >= 2 * PER_CYCLE);

    std::printf("  rearm_cycle:                               "
                "PASSED (total=%llu over 2 cycles)\n",
                static_cast<unsigned long long>(bt.total_processed.load_acquire()));
}

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
static void test_reset_drops_inflight_regions() {
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
static void test_callback_runs_outside_arena_gate() {
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

// The retained-region queue is bounded, so a process that publishes forever
// holds a constant number of pointers rather than one per region.  Driving
// past the bound directly is the only way to see the wrap.
static void test_uncompiled_queue_is_bounded() {
    BackgroundThread::UncompiledRegionQueue queue;
    constexpr uint32_t CAP = BackgroundThread::UncompiledRegionQueue::CAP;

    assert(queue.size() == 0u);
    assert(queue.total() == 0u);

    // The pointers are never dereferenced by the queue, so distinct
    // non-null bit patterns stand in for regions.
    auto fake = [](uint64_t i) { return std::bit_cast<crucible::RegionNode*>(uintptr_t{0x1000} + i * 0x40); };

    for (uint64_t i = 0; i < CAP; ++i) {
        queue.push(fake(i));
        assert(queue.total() == i + 1);
        assert(queue.size() == i + 1);
        assert(queue.at(0) == fake(i) && "age 0 is the newest");
    }

    // One past the bound: the total keeps counting, the retention does not.
    constexpr uint64_t OVERRUN = 4u * CAP + 3u;
    for (uint64_t i = CAP; i < OVERRUN; ++i)
        queue.push(fake(i));

    assert(queue.total() == OVERRUN);
    assert(queue.size() == CAP && "retention must stop at the bound");
    for (uint32_t age = 0; age < CAP; ++age)
        assert(queue.at(age) == fake(OVERRUN - 1 - age) && "the bound keeps the newest CAP, in order");

    std::printf("  uncompiled_queue_bounded:                  "
                "PASSED (pushed=%llu, retained=%u)\n",
                static_cast<unsigned long long>(queue.total()), queue.size());
}

int main() {
    std::printf("test_background_thread_run_in_row — effect-row fence\n");
    test_t01_required_row_pinned();
    test_t02_context_matrix();
    test_t03_api_surface_pinned();
    test_t04_runtime_smoke();

    test_cross_fence_consistency();
    test_concurrent_spsc_drain();
    test_large_batch_drain();
    test_rearm_cycle();
    // These three run last because start() seals the global schema and
    // kernel tables, which every group above deliberately avoids.
    test_uncompiled_queue_is_bounded();
    test_reset_drops_inflight_regions();
    test_callback_runs_outside_arena_gate();

    std::printf("test_background_thread_run_in_row: 11 groups, all passed\n");
    return 0;
}
