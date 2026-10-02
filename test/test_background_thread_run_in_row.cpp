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
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include "test_assert.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

using crucible::BackgroundThread;
namespace eff = ::foundation::effects;

namespace test_background_thread_run_in_row {

namespace {

void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<BackgroundThread::run_required_row,
                                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>,
                  "BackgroundThread::run_required_row is exactly Row<Bg, Alloc, IO, Block>.");

    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Bg));
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Alloc));
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::IO));
    static_assert(eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Block));

    // Init and Test tag other entry points.  The drain loop exhibits neither.
    static_assert(!eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Init));
    static_assert(!eff::row_contains(^^BackgroundThread::run_required_row, eff::Effect::Test));

    static_assert(eff::row_size(^^BackgroundThread::run_required_row) == 4);

    std::printf("  T01 required_row_pinned:                   PASSED\n");
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

    std::printf("  T02 context_matrix:                        PASSED\n");
}

// The pointer is never dereferenced.  The call expression sits inside
// decltype, where it is unevaluated, and only its type is taken.
void test_t03_api_surface_pinned() {
    BackgroundThread* bt_ptr = nullptr;
    using ReturnT = decltype(bt_ptr->run_in_row(std::declval<::fixy::BgLoadCtx const&>()));
    static_assert(std::is_same_v<ReturnT, void>, "run_in_row(ctx) returns void.");
    static_assert(noexcept(bt_ptr->run_in_row(std::declval<::fixy::BgLoadCtx const&>())),
                  "run_in_row(ctx) is noexcept, because the pipeline thread runs it as its entry.");

    std::printf("  T03 api_surface_pinned:                    PASSED\n");
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

    // Containment runs one way only.  The drain row additionally admits Bg
    // and Alloc, so the two rows are not equal.
    static_assert(!eff::Subrow<BgRow, RecordRow>, "The background drain row strictly contains the record_event row: "
                                                  "Bg and Alloc are extra.");
    static_assert(eff::row_size(^^BgRow) == 4u);
    static_assert(eff::row_size(^^RecordRow) == 2u);

    std::printf("  cross_fence_consistency:                   PASSED\n");
}

// The retained-region queue is bounded, so a process that publishes forever
// holds a constant number of pointers rather than one per region.  Driving
// past the bound directly is the only way to see the wrap.
void test_uncompiled_queue_is_bounded() {
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

}  // namespace

}  // namespace test_background_thread_run_in_row

int main() {
    using namespace test_background_thread_run_in_row;
    std::printf("test_background_thread_run_in_row — effect-row fence\n");
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
    test_reset_drops_inflight_regions();
    test_callback_runs_outside_arena_gate();
    test_overflow_build_leaves_tail();
    test_publish_stage_releases_in_order();
    test_pipeline_releases_after_overflow();
    test_built_graph_owns_its_metadata();
    test_build_and_release_pass_two_to_the_32();

    std::printf("test_background_thread_run_in_row: 16 groups, all passed\n");
    return 0;
}
