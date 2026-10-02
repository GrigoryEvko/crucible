#pragma once

// The shared part of test_background_thread_run_in_row: the background load
// context of the tests, and the tests of each group.  The checks of the row,
// the bounded queue and main are in test_background_thread_run_in_row.cpp.
// Each other source file of the test holds one group of tests.

#include <crucible/BackgroundThread.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace test_background_thread_run_in_row {

// The background load context, over the source the test witness hands out.
[[nodiscard]] inline ::fixy::BgLoadCtx bg_load_ctx() noexcept {
    return ::fixy::BgLoadCtx{::foundation::effects::testing::bg()};
}

// The drain loop behind run_in_row
// (test_background_thread_run_in_row_drain.cpp).
void test_t04_runtime_smoke();
void test_concurrent_spsc_drain();
void test_large_batch_drain();
void test_rearm_cycle();

// The divergence reset and the publish stage
// (test_background_thread_run_in_row_publish.cpp).
void test_reset_drops_inflight_regions();
void test_callback_runs_outside_arena_gate();

// The release of the metadata log, and the copy of its records that a built
// graph holds (test_background_thread_run_in_row_meta.cpp).
void test_overflow_build_leaves_tail();
void test_publish_stage_releases_in_order();
void test_pipeline_releases_after_overflow();
void test_built_graph_owns_its_metadata();
void test_build_and_release_pass_two_to_the_32();

}  // namespace test_background_thread_run_in_row
