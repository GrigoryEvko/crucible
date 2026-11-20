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

#include "background_thread_run_in_row.h"

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

// The pointer is never dereferenced.  The call expression sits inside
// decltype, where it is unevaluated, and only its type is taken.
void test_t03_api_surface_pinned() {
    BackgroundThread* bt_ptr = nullptr;
    using ReturnT = decltype(bt_ptr->run_in_row(std::declval<::fixy::BgLoadCtx const&>()));
    static_assert(std::is_same_v<ReturnT, void>, "run_in_row(ctx) returns void.");
    static_assert(noexcept(bt_ptr->run_in_row(std::declval<::fixy::BgLoadCtx const&>())),
                  "run_in_row(ctx) is noexcept, because the pipeline thread runs it as its entry.");

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

// The retained-region queue is bounded, so a process that publishes forever
// holds a constant number of pointers rather than one per region.  Driving
// past the bound directly is the only way to see the wrap.
void test_uncompiled_queue_is_bounded() {
    constexpr uint32_t CAP = crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE;
    BackgroundThread::UncompiledRegionQueue queue{::foundation::effects::testing::test().alloc,
                                                  ::fixy::Quantity<::fixy::unit::Element, uint32_t>{CAP}};

    CRUCIBLE_TEST_ASSERT(kTest, queue.size() == 0u);
    CRUCIBLE_TEST_ASSERT(kTest, queue.total() == 0u);

    // The queue never reads a region, so default nodes stand in for the
    // published regions.  Each push names a node of its own.
    constexpr uint64_t OVERRUN = 4u * CAP + 3u;
    static ::fixy::FixedArray<crucible::RegionNode, OVERRUN> regions{};
    auto region_at = [](uint64_t i) {
        return ::fixy::some(
            ::fixy::erase_brand(regions.get(i).expect("a test reads an element at or past the count of its array")));
    };

    for (uint64_t i = 0; i < CAP; ++i) {
        queue.push(*regions.get(i).expect("a test reads an element at or past the count of its array"));
        CRUCIBLE_TEST_ASSERT(kTest, queue.total() == i + 1);
        CRUCIBLE_TEST_ASSERT(kTest, queue.size() == i + 1);
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(queue.at(0), region_at(i)) && "age 0 is the newest");
    }

    // One past the bound: the total keeps counting, the retention does not.
    for (uint64_t i = CAP; i < OVERRUN; ++i)
        queue.push(*regions.get(i).expect("a test reads an element at or past the count of its array"));

    CRUCIBLE_TEST_ASSERT(kTest, queue.total() == OVERRUN);
    CRUCIBLE_TEST_ASSERT(kTest, queue.size() == CAP && "retention must stop at the bound");
    for (uint32_t age = 0; age < CAP; ++age)
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_target(queue.at(age), region_at(OVERRUN - 1 - age))
                                        && "the bound keeps the newest CAP, in order");

    ::fixy::check::pass(kTest,
                        "  uncompiled_queue_bounded:                  "
                        "PASSED (pushed={}, retained={})\n",
                        static_cast<unsigned long long>(queue.total()), queue.size());
}

// The capacity of the queue is a limit of the pipeline.  A queue of three
// keeps the three newest regions and drops each older one.  A queue of zero
// keeps no region and only counts.
void test_uncompiled_queue_refuses_past_its_capacity(::fixy::TestRunnerCtx const& test) {
    using Count = ::fixy::Quantity<::fixy::unit::Element, uint32_t>;
    static ::fixy::FixedArray<crucible::RegionNode, 8> regions{};
    BackgroundThread::UncompiledRegionQueue three{::foundation::effects::testing::test().alloc, Count{3}};
    BackgroundThread::UncompiledRegionQueue none{::foundation::effects::testing::test().alloc, Count{0}};
    CRUCIBLE_TEST_ASSERT(test, three.capacity() == 3 && none.capacity() == 0);
    for (uint64_t i = 0; i < 8; ++i) {
        three.push(*regions.get(i).expect("a test reads an element at or past the count of its array"));
        none.push(*regions.get(i).expect("a test reads an element at or past the count of its array"));
    }
    CRUCIBLE_TEST_ASSERT(test, three.total() == 8 && three.size() == 3);
    for (uint32_t age = 0; age < 3; ++age) {
        CRUCIBLE_TEST_ASSERT(
            test, ::fixy::same_target(three.at(age), ::fixy::some(regions.get(uint64_t{7} - age)
                                                                      .expect("a test reads an element at or past "
                                                                              "the count of its array"))));
    }
    CRUCIBLE_TEST_ASSERT(test, none.total() == 8 && none.size() == 0);
    ::fixy::check::pass(test, "  uncompiled_queue_refuses_past_its_capacity: PASSED\n");
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
    test_uncompiled_queue_refuses_past_its_capacity(::fixy::TestRunnerCtx{::foundation::effects::testing::test()});
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

    ::fixy::check::pass(kTest, "test_background_thread_run_in_row: 21 groups, all passed\n");
    return 0;
}
