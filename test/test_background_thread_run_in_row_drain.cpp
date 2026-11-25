// The drain loop behind run_in_row of test_background_thread_run_in_row: a
// smoke run, a concurrent drain, a large batch, and a second spawn.

#include "background_thread_run_in_row.h"
#include "trace_rig.h"

#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <chrono>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;

namespace test_background_thread_run_in_row {

namespace {

constexpr auto kAlloc = ::foundation::effects::testing::test().alloc;

// Appends one op with no record.  The push side has a fence of its own, which
// is not what is under test here, so this goes through the untyped append.
void append_op(crucible::test::TraceRig& rig, uint64_t schema, uint64_t shape) noexcept {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema};
    entry.shape_hash = crucible::ShapeHash{shape};
    while (!rig.append_entry(entry, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Waits until the pipeline processed the count of ops.  The processed counter
// is what says that the drain consumed every entry.  The deadline keeps a
// stalled drain from hanging the suite.
void wait_for_count(BackgroundThread const& bt, uint64_t count) noexcept {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bt.total_processed.load_acquire() < count && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= count);
}

}  // namespace

// Stop is signalled before the loop starts, so the loop body never runs and
// the call falls straight through to the trailing drain, which is empty.
// What is under test is that the entry point forwards at all, on a thread of
// its own.
void test_t04_runtime_smoke() {
    crucible::test::TraceRig rig{};
    crucible::with_background_thread(
        kAlloc, rig.ring_side(), rig.log_side(), {}, [](BackgroundThread const& bt, auto stopped) noexcept {
            bt.stop_requested.signal();
            CRUCIBLE_TEST_ASSERT(kTest, stopped
                                            .run_in_row_beside(bg_load_ctx(), ::fixy::thread::ThreadOptions{},
                                                               [](BackgroundThread const& /*shared*/) noexcept {})
                                            .is_ok());
        });

    ::fixy::check::pass(kTest, "  T04 runtime_smoke:                         PASSED\n");
}

// A real drain, with one thread pushing while the entry point consumes.
// What it establishes is that the entry point forwards rather than
// reimplements, so the queue's acquire and release discipline is untouched
// and the trailing drain does not hang.
//
// Nothing here builds a region.  The entries carry no tensor metadata, so the
// iteration detector advances but never closes anything, which leaves plain
// drain motion to observe.
void test_concurrent_spsc_drain() {
    // Each entry carries its own schema hash so the iteration detector never
    // matches a signature.
    constexpr uint32_t N = 8;
    crucible::test::TraceRig rig{};
    crucible::with_background_thread(
        kAlloc, rig.ring_side(), rig.log_side(), {}, [&rig](BackgroundThread const& /*bt*/, auto stopped) noexcept {
            stopped.rearm_stop();
            CRUCIBLE_TEST_ASSERT(kTest, stopped
                                            .run_in_row_beside(bg_load_ctx(), ::fixy::thread::ThreadOptions{},
                                                               [&rig](BackgroundThread const& shared) noexcept {
                                                                   for (uint32_t i = 0; i < N; ++i)
                                                                       append_op(rig, 0x1000ULL + i, 0x2000ULL + i);
                                                                   wait_for_count(shared, N);
                                                               })
                                            .is_ok());
        });

    ::fixy::check::pass(kTest, "  concurrent_spsc_drain:                     PASSED\n");
}

// A batch large enough to span several drain batches, to catch a loop that
// stops before consuming everything it drained.
//
// The hashes are all distinct so the iteration detector never fires.  Taking
// the boundary path would need the global schema and kernel tables sealed,
// which the normal start sequence does and this test deliberately bypasses in
// order to reach the entry point directly.
void test_large_batch_drain() {
    constexpr uint32_t TOTAL = 32;
    crucible::test::TraceRig rig{};
    const uint64_t processed_after_push = crucible::with_background_thread(
        kAlloc, rig.ring_side(), rig.log_side(), {}, [&rig](BackgroundThread const& bt, auto stopped) noexcept {
            stopped.rearm_stop();
            uint64_t processed = 0;
            CRUCIBLE_TEST_ASSERT(kTest,
                                 stopped
                                     .run_in_row_beside(bg_load_ctx(), ::fixy::thread::ThreadOptions{},
                                                        [&rig, &processed](BackgroundThread const& shared) noexcept {
                                                            for (uint32_t i = 0; i < TOTAL; ++i)
                                                                append_op(rig, 0xABCD0001ULL + i, 0xDEAD0001ULL + i);
                                                            wait_for_count(shared, TOTAL);
                                                            processed = shared.total_processed.load_acquire();
                                                        })
                                     .is_ok());

            // No signature ever matched twice, so the boundary path never ran
            // and the completed count stays at zero.
            CRUCIBLE_TEST_ASSERT(kTest, bt.iterations_completed.get() == 0u);
            return processed;
        });

    ::fixy::check::pass(kTest,
                        "  large_batch_drain:                         "
                        "PASSED (processed={} of {})\n",
                        static_cast<unsigned long long>(processed_after_push), TOTAL);
}

// Spawn, stop, spawn again.  A once-flag, a static guard inside the template
// instantiation, or a destructor that failed to run would leave the second
// invocation dead, and the second cycle would then drain nothing.
void test_rearm_cycle() {
    constexpr uint32_t PER_CYCLE = 4;
    crucible::test::TraceRig rig{};
    const uint64_t total = crucible::with_background_thread(
        kAlloc, rig.ring_side(), rig.log_side(), {}, [&rig](BackgroundThread const& bt, auto stopped) noexcept {
            uint64_t prev_processed = 0;
            for (uint32_t cycle = 0; cycle < 2; ++cycle) {
                stopped.rearm_stop();
                CRUCIBLE_TEST_ASSERT(
                    kTest,
                    stopped
                        .run_in_row_beside(bg_load_ctx(), ::fixy::thread::ThreadOptions{},
                                           [&rig, cycle, prev_processed](BackgroundThread const& shared) noexcept {
                                               // The cycle index enters the hash so the detector cannot tie
                                               // the two cycles together into one signature.
                                               for (uint32_t i = 0; i < PER_CYCLE; ++i)
                                                   append_op(rig, 0x10000ULL + (cycle << 16) + i, 0x20000ULL + i);
                                               wait_for_count(shared, prev_processed + PER_CYCLE);
                                           })
                        .is_ok());
                prev_processed = bt.total_processed.load_acquire();
            }

            // Both invocations consumed entries, so the total covers both
            // cycles.
            CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= 2 * PER_CYCLE);
            return bt.total_processed.load_acquire();
        });

    ::fixy::check::pass(kTest,
                        "  rearm_cycle:                               "
                        "PASSED (total={} over 2 cycles)\n",
                        static_cast<unsigned long long>(total));
}

}  // namespace test_background_thread_run_in_row
