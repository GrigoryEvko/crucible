// The background drain loop exhibits four effect atoms: the background
// context tag itself, arena allocation while building a region, output
// through the region-ready callback, and a wait on the arena gate.  A caller
// therefore hands in a context whose row admits those four.
//
// The context is the evidence.  The pipeline thread takes it from the door
// of the background context, and a test takes it from the test witness.
// This file holds the accepting side.  The rejecting side needs a compile to
// fail, so it lives with the negative fixtures.
//
// The test is several source files of one executable, so that no
// translation unit holds every test:
//
//   background_thread_run_in_row.h      the shared part
//   this file                           the checks of the row, the bounded
//                                       queue and main
//   ..._drain.cpp                       the drain loop behind run_in_row
//   ..._publish.cpp                     the divergence reset and the
//                                       publish stage
//   ..._meta.cpp                        the release of the metadata log, and
//                                       the copy of its records
//   ..._sweep.cpp                       the sweep of the region store with a
//                                       foreground

#include "background_thread_run_in_row.h"
#include "trace_rig.h"

#include <crucible/BackgroundThread.h>
#include <crucible/Cipher.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
namespace eff = ::foundation::effects;

namespace test_background_thread_run_in_row {

namespace {

// A region node with each field at its default, in the arena, which one frame
// on the arena builds.  A queue of the publish stage takes it as a stand-in
// for a published region.
[[nodiscard]] crucible::DagRef<crucible::RegionNode> mint_region_node(eff::Alloc alloc,
                                                                      ::fixy::BumpArena<crucible::DagArena>& arena) {
    return ::foundation::core::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        return crucible::mint_dag_node<crucible::RegionNode>(alloc, frame).weaken();
    });
}

void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<BackgroundThread::run_required_row,
                                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
                                          eff::Effect::Nondet, eff::Effect::Div>>,
                  "BackgroundThread::run_required_row is exactly Row<Bg, Alloc, IO, Block, Nondet, Div>.");

    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Bg));
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Alloc));
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::IO));
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Block));
    // The pipeline waits to a deadline and commits with a time stamp, so it
    // reads a clock.
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Nondet));

    // Init and Test tag other entry points.  The drain loop exhibits neither.
    static_assert(!eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Init));
    static_assert(!eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Test));

    // The drain loop runs until the stop, with no bound.
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Div));

    static_assert(eff::row_size(^^BackgroundThread::run_required_row) == 6);

    ::fixy::check::pass(kTest, "  T01 required_row_pinned:                   PASSED\n");
}

// The one context that admits the row, and the contexts that do not.  These
// check the predicate.  Rejecting an actual call needs a substitution
// failure, which only a fixture that fails to compile can show.
void test_t02_context_matrix() {
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

    ::fixy::check::pass(kTest, "  T02 context_matrix:                        PASSED\n");
}

// The brand of a Stopped handle that no frame gives.
struct ProbeScope final {};

// The call expression sits inside decltype, where it is unevaluated, and only
// its type is taken.
void test_t03_api_surface_pinned() {
    using Stopped = BackgroundThread::Stopped<ProbeScope>;
    using ReturnT = decltype(std::declval<Stopped&>().run_in_row(std::declval<::fixy::BgLoadCtx const&>()));
    static_assert(std::is_same_v<ReturnT, void>, "run_in_row(ctx) returns void.");
    static_assert(noexcept(std::declval<Stopped&>().run_in_row(std::declval<::fixy::BgLoadCtx const&>())),
                  "run_in_row(ctx) is noexcept, because a thread body runs it.");

    ::fixy::check::pass(kTest, "  T03 api_surface_pinned:                    PASSED\n");
}

// The drain loop calls record_event while committing a region, so by
// transitivity its row has to contain record_event's row.  Narrowing the
// drain row would otherwise leave that downstream call unsatisfiable, and
// this inclusion fires before any caller reaches the violation itself.
void test_cross_fence_consistency() {
    using BgRow = BackgroundThread::run_required_row;
    using RecordRow = ::crucible::Cipher::record_event_required_row;

    static_assert(eff::Subrow<RecordRow, BgRow>, "BackgroundThread::run_required_row contains "
                                                 "Cipher::record_event_required_row. The background drain calls "
                                                 "record_event while committing a region, so a drain row that does "
                                                 "not admit IO and Block leaves that call site unsatisfiable.");

    // Containment runs one way only.  The drain row additionally admits Bg,
    // Alloc, Nondet and Div, so the two rows are not equal.
    static_assert(!eff::Subrow<BgRow, RecordRow>, "The background drain row strictly contains the record_event row: "
                                                  "Bg, Alloc, Nondet and Div are extra.");
    static_assert(eff::row_size(^^BgRow) == 6u);
    static_assert(eff::row_size(^^RecordRow) == 2u);

    ::fixy::check::pass(kTest, "  cross_fence_consistency:                   PASSED\n");
}

// Runs the job on the publish stage of a thread that runs no pipeline, so the
// job runs on this thread with the proof.
template <class Job>
void run_publish_job(Job job) {
    crucible::test::TraceRig rig{};
    crucible::with_background_thread(
        eff::testing::test().alloc, rig.ring_side(), rig.log_side(), {},
        [&job](BackgroundThread const& /*bt*/, auto stopped) noexcept { stopped.run_on_publish_stage(job); });
}

// The retained-region queue is bounded, so a process that publishes forever
// holds a constant number of pointers rather than one per region.  Driving
// past the bound directly is the only way to see the wrap.  A read of a slot
// takes the publish-stage proof.
void test_uncompiled_queue_is_bounded() {
    run_publish_job([](BackgroundThread::PublishStageView&, BackgroundThread::PublishStage const& stage) noexcept {
        constexpr uint32_t CAP = crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE;
        const auto alloc = ::foundation::effects::testing::test().alloc;
        // The queue keeps the address of each node that it takes, so the nodes
        // come from an arena that outlives the queue.
        ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
            ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 16});
        BackgroundThread::UncompiledRegions queue{alloc, ::fixy::Quantity<::fixy::unit::Element, uint32_t>{CAP}};

        CRUCIBLE_TEST_ASSERT(kTest, queue.size() == 0u);
        CRUCIBLE_TEST_ASSERT(kTest, queue.total() == 0u);

        // The queue never reads a region, so default nodes stand in for the
        // published regions.  Each push names a node of its own.
        constexpr uint64_t OVERRUN = 4u * CAP + 3u;
        auto links = arena.array(alloc, OVERRUN, crucible::RegionLink{});
        const ::fixy::View<crucible::RegionLink> regions = links.view();
        for (crucible::RegionLink& link : regions)
            link = ::fixy::some(mint_region_node(alloc, arena));
        auto region_at = [regions](uint64_t i) {
            return *regions.get(i).expect("a test reads an element at or past the count of its array");
        };

        // The slot of a push is the number of the push here, because the queue
        // only gives it back.
        auto push_region = [&queue, &stage, &region_at](uint64_t i) {
            return queue.push(stage, region_at(i).expect("each link holds a node"),
                              crucible::RegionSlot{static_cast<uint32_t>(i)});
        };
        for (uint64_t i = 0; i < CAP; ++i) {
            CRUCIBLE_TEST_ASSERT(kTest, push_region(i).pushed_out.is_none() && "a free entry held no region");
            CRUCIBLE_TEST_ASSERT(kTest, queue.total() == i + 1);
            CRUCIBLE_TEST_ASSERT(kTest, queue.size() == i + 1);
            CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(queue.at(stage, 0), region_at(i)) && "age 0 is the newest");
        }

        // One past the bound: the total keeps counting, the retention does not.
        // Each push takes out the oldest region, and gives its slot.
        for (uint64_t i = CAP; i < OVERRUN; ++i) {
            CRUCIBLE_TEST_ASSERT(kTest, push_region(i).pushed_out
                                            == ::fixy::some(crucible::RegionSlot{static_cast<uint32_t>(i - CAP)}));
        }

        CRUCIBLE_TEST_ASSERT(kTest, queue.total() == OVERRUN);
        CRUCIBLE_TEST_ASSERT(kTest, queue.size() == CAP && "retention must stop at the bound");
        for (uint32_t age = 0; age < CAP; ++age)
            CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(queue.at(stage, age), region_at(OVERRUN - 1 - age))
                                            && "the bound keeps the newest CAP, in order");

        ::fixy::check::pass(kTest,
                            "  uncompiled_queue_bounded:                  "
                            "PASSED (pushed={}, retained={})\n",
                            static_cast<unsigned long long>(queue.total()), queue.size());
    });
}

// The capacity of the queue is a limit of the pipeline.  A queue of three
// keeps the three newest regions and drops each older one.  A queue of zero
// keeps no region and only counts.  The reads run in a job, as above.
void test_uncompiled_queue_refuses_past_its_capacity() {
    run_publish_job([](BackgroundThread::PublishStageView&, BackgroundThread::PublishStage const& stage) noexcept {
        using Count = ::fixy::Quantity<::fixy::unit::Element, uint32_t>;
        const auto alloc = ::foundation::effects::testing::test().alloc;
        // The queues keep the address of each node, so the nodes come from
        // an arena that outlives the queues.
        ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
            ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 12});
        auto links = arena.array(alloc, 8, crucible::RegionLink{});
        const ::fixy::View<crucible::RegionLink> regions = links.view();
        for (crucible::RegionLink& link : regions)
            link = ::fixy::some(mint_region_node(alloc, arena));
        BackgroundThread::UncompiledRegions three{alloc, Count{3}};
        BackgroundThread::UncompiledRegions none{alloc, Count{0}};
        CRUCIBLE_TEST_ASSERT(kTest, three.capacity() == 3 && none.capacity() == 0);
        for (uint64_t i = 0; i < 8; ++i) {
            const crucible::DagRef<crucible::RegionNode> node =
                crucible::RegionLink{
                    *regions.get(i).expect("a test reads an element at or past the count of its array")}
                    .expect("each link holds a node");
            const crucible::RegionSlot slot{static_cast<uint32_t>(i)};
            static_cast<void>(three.push(stage, node, slot));
            CRUCIBLE_TEST_ASSERT(kTest, none.push(stage, node, slot).pushed_out.is_none()
                                            && "a queue of no entry takes no region out");
        }
        CRUCIBLE_TEST_ASSERT(kTest, three.total() == 8 && three.size() == 3);
        for (uint32_t age = 0; age < 3; ++age) {
            CRUCIBLE_TEST_ASSERT(
                kTest, ::fixy::same_target(three.at(stage, age), *regions.get(uint64_t{7} - age)
                                                                      .expect("a test reads an element at or past "
                                                                              "the count of its array")));
        }
        CRUCIBLE_TEST_ASSERT(kTest, none.total() == 8 && none.size() == 0);
        ::fixy::check::pass(kTest, "  uncompiled_queue_refuses_past_its_capacity: PASSED\n");
    });
}

// True when a walk of the slots of the queue finds the region.  It is the
// oracle of UncompiledRegionQueue::holds.  Complexity: O(Q) for Q slots.
bool walk_finds(BackgroundThread::UncompiledRegions const& queue, BackgroundThread::PublishStage const& stage,
                crucible::RegionLink region) {
    for (uint32_t age = 0; age < queue.size(); ++age) {
        if (::fixy::same_target(queue.at(stage, age), region)) return true;
    }
    return false;
}

// The answer of the queue for each push, with a push after each check.  The
// region of the push with capacity - 1 pushes after it is held, and the one
// with capacity pushes after it left the queue.
void check_queue_against_its_walk(BackgroundThread::PublishStage const& stage,
                                  ::fixy::Quantity<::fixy::unit::Element, uint32_t> capacity) {
    constexpr uint32_t PUSHES = 2u * crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE + 5u;
    const auto alloc = ::foundation::effects::testing::test().alloc;
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 16});
    BackgroundThread::UncompiledRegions queue{alloc, capacity};
    ::fixy::FixedArray<crucible::QueuedRegion, PUSHES> pushes{};
    for (const ::fixy::Index<PUSHES> position : pushes.indices()) {
        CRUCIBLE_TEST_ASSERT(kTest, !queue.holds(stage, pushes.at(position)) && "no queue holds the region of no push");
        crucible::QueuePush pushed = queue.push(stage, mint_region_node(alloc, arena),
                                                crucible::RegionSlot{static_cast<uint32_t>(position.raw())});
        // The push takes out the region of the push capacity pushes before it.
        const bool takes_out = capacity.raw() != 0 && position.raw() >= capacity.raw();
        CRUCIBLE_TEST_ASSERT(kTest, pushed.pushed_out.is_some() == takes_out);
        CRUCIBLE_TEST_ASSERT(kTest,
                             pushed.pushed_out.is_none_or([&position, capacity](crucible::RegionSlot out) noexcept {
                                 return out.raw() == position.raw() - capacity.raw();
                             }));
        static_cast<void>(pushes.exchange(position, std::move(pushed.queued)));
        uint32_t held = 0;
        for (const ::fixy::Index<PUSHES> earlier : pushes.indices()) {
            if (earlier.raw() > position.raw()) break;
            crucible::QueuedRegion const& queued = pushes.at(earlier);
            const bool is_held = queue.holds(stage, queued);
            CRUCIBLE_TEST_ASSERT(kTest, is_held == walk_finds(queue, stage, queued.node()));
            CRUCIBLE_TEST_ASSERT(kTest, is_held == (position.raw() - earlier.raw() < capacity.raw()));
            held += is_held ? 1u : 0u;
        }
        CRUCIBLE_TEST_ASSERT(kTest, held == queue.size());
    }
}

// The count of a push answers what a walk of the slots answers, for each push
// and past the capacity of the queue.  A queue of zero holds no region.
void test_uncompiled_queue_answers_as_its_walk() {
    run_publish_job([](BackgroundThread::PublishStageView&, BackgroundThread::PublishStage const& stage) noexcept {
        using Count = ::fixy::Quantity<::fixy::unit::Element, uint32_t>;
        check_queue_against_its_walk(stage, Count{0});
        check_queue_against_its_walk(stage, Count{1});
        check_queue_against_its_walk(stage, Count{3});
        check_queue_against_its_walk(stage, Count{crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE});
    });
    ::fixy::check::pass(kTest, "  uncompiled_queue_answers_as_its_walk:      PASSED\n");
}

// A move of a QueuedRegion leaves its source with no push, so one push stands in
// one place.  Each case moves the pushes between the slots of an array and then
// asks the queue for each slot, so it reads each source through the array.
//
// An exchange assigns each slot that it leaves, so it shows no source.  The
// frame of a HeapArray gives two parts that do not overlap: a move from the one
// part to the other leaves its source in the slot.  One push goes by the move
// assignment, and the other one by the move constructor.
void test_moved_push_leaves_no_push_in_its_source() {
    run_publish_job([](BackgroundThread::PublishStageView&, BackgroundThread::PublishStage const& stage) noexcept {
        using Count = ::fixy::Quantity<::fixy::unit::Element, uint32_t>;
        const auto alloc = ::foundation::effects::testing::test().alloc;
        ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
            ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 12});
        BackgroundThread::UncompiledRegions queue{alloc, Count{4}};
        auto push_new = [&queue, &stage, &arena, alloc](uint32_t slot) {
            return queue.push(stage, mint_region_node(alloc, arena), crucible::RegionSlot{slot}).queued;
        };

        ::fixy::HeapArray<crucible::QueuedRegion> slots = ::fixy::mint_heap_array_with<crucible::QueuedRegion>(
            alloc, 4, [](std::size_t) noexcept { return crucible::QueuedRegion{}; });
        static_cast<void>(slots.exchange(0, push_new(0)));
        static_cast<void>(slots.exchange(1, push_new(1)));
        slots = ::fixy::with_mut(std::move(slots), [](auto run) noexcept {
            auto [sources, targets] = ::fixy::split_at(std::move(run), 2).expect("the array has four slots");
            auto assigned = sources.get_mut(0).expect("a part of two slots");
            auto first_target = targets.get_mut(0).expect("a part of two slots");
            *first_target = std::move(*assigned);
            auto constructed = sources.get_mut(1).expect("a part of two slots");
            auto second_target = targets.get_mut(1).expect("a part of two slots");
            crucible::QueuedRegion taken{std::move(*constructed)};
            *second_target = std::move(taken);
        });
        for (std::size_t position = 0; position < slots.size(); ++position) {
            crucible::QueuedRegion const& queued = *slots.get(position).expect("the array has four slots");
            CRUCIBLE_TEST_ASSERT(kTest, queue.holds(stage, queued) == (position >= 2));
            CRUCIBLE_TEST_ASSERT(kTest, queued.node().is_some() == (position >= 2));
        }

        // An exchange of two slots keeps the two pushes, each one time.
        ::fixy::FixedArray<crucible::QueuedRegion, 2> pair{};
        static_cast<void>(pair.exchange(::fixy::index<0>(), push_new(2)));
        static_cast<void>(pair.exchange(::fixy::index<1>(), push_new(3)));
        const crucible::RegionLink first = pair.at(::fixy::index<0>()).node();
        const crucible::RegionLink second = pair.at(::fixy::index<1>()).node();
        crucible::QueuedRegion taken = pair.exchange(::fixy::index<0>(), crucible::QueuedRegion{});
        CRUCIBLE_TEST_ASSERT(kTest, !queue.holds(stage, pair.at(::fixy::index<0>())));
        static_cast<void>(pair.exchange(::fixy::index<0>(), pair.exchange(::fixy::index<1>(), std::move(taken))));
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(pair.at(::fixy::index<0>()).node(), second));
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(pair.at(::fixy::index<1>()).node(), first));
        for (crucible::QueuedRegion const& queued : pair)
            CRUCIBLE_TEST_ASSERT(kTest, queue.holds(stage, queued));
    });
    ::fixy::check::pass(kTest, "  moved_push_leaves_no_push_in_its_source:   PASSED\n");
}

}  // namespace

}  // namespace test_background_thread_run_in_row

int main() {
    using namespace test_background_thread_run_in_row;
    ::fixy::report(::fixy::Sink::Out, "test_background_thread_run_in_row — effect-row fence\n");
    test_t01_required_row_pinned();
    test_t02_context_matrix();
    test_t03_api_surface_pinned();
    test_t04_runtime_smoke();

    test_cross_fence_consistency();
    test_concurrent_spsc_drain();
    test_large_batch_drain();
    test_rearm_cycle();
    // These run last because they seal the global schema and kernel tables,
    // which every group above deliberately avoids.
    test_uncompiled_queue_is_bounded();
    test_uncompiled_queue_refuses_past_its_capacity();
    test_uncompiled_queue_answers_as_its_walk();
    test_moved_push_leaves_no_push_in_its_source();
    test_limits_refuse_what_passes_them(::fixy::TestRunnerCtx{::foundation::effects::testing::test()});
    test_reset_drops_inflight_regions();
    test_reset_applies_at_its_point();
    test_mark_keeps_an_unapplied_restart();
    test_reset_skips_the_stale_backlog();
    test_published_regions_keep_their_hashes();
    test_long_run_keeps_its_scratch();
    test_overflow_build_leaves_tail();
    test_unrecorded_nest_refuses_the_build();
    test_publish_stage_releases_in_order();
    test_pipeline_releases_after_overflow();
    test_built_graph_owns_its_metadata();
    test_build_and_release_pass_two_to_the_32();
    test_scalars_of_an_op_with_no_tensors();
    test_publish_adds_no_block_to_the_region_arena();
    test_scratch_bytes_beside_a_publish();
    test_store_releases_what_left_the_queue();
    test_sweep_frees_known_slots();
    test_sweep_waits_for_no_write();
    test_region_handed_again_keeps_no_old_mark();
    test_kept_region_keeps_no_old_mark();
    test_sweep_matches_a_walk_of_each_region();

    ::fixy::check::pass(kTest, "test_background_thread_run_in_row: 37 groups, all passed\n");
    return 0;
}
