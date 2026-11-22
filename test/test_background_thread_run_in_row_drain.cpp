// The drain loop behind run_in_row of test_background_thread_run_in_row: a
// smoke run, a concurrent drain, a large batch, and a second spawn.

#include "background_thread_run_in_row.h"

#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
using crucible::TraceRing;
using crucible::MetaLog;

namespace test_background_thread_run_in_row {

// Stop is signalled before the loop starts, so the loop body never runs and
// the call falls straight through to the trailing drain, which is empty.
// What is under test is that the entry point forwards at all.
void test_t04_runtime_smoke() {
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>(::foundation::effects::testing::test().alloc);

    bt.bind_ring(*ring);
    bt.bind_meta_log(*metalog);
    bt.stop_requested.signal();

    bool done = false;
    ::fixy::thread::QuiescenceProof<BackgroundThread> const proof = bt.prove_quiescence();
    std::jthread t([&]() {
        bt.run_in_row(proof, bg_load_ctx());
        done = true;
    });
    t.join();
    CRUCIBLE_TEST_ASSERT(kTest, done);

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
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>(::foundation::effects::testing::test().alloc);
    bt.bind_ring(*ring);
    bt.bind_meta_log(*metalog);
    bt.rearm_stop();

    ::fixy::thread::QuiescenceProof<BackgroundThread> const proof = bt.prove_quiescence();
    std::jthread bg_thread([&]() { bt.run_in_row(proof, bg_load_ctx()); });

    // Each entry carries its own schema hash so the iteration detector never
    // matches a signature.
    constexpr uint32_t N = 8;
    for (uint32_t i = 0; i < N; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0x1000ULL + i};
        e.shape_hash = crucible::ShapeHash{0x2000ULL + i};
        // The push side has a fence of its own, which is not what is under
        // test here, so this goes through the untyped append.
        while (!ring->try_append(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // The processed counter reaching N is what says the drain consumed every
    // entry.  The deadline keeps a stalled drain from hanging the suite.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bt.total_processed.load_acquire() < N && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= N);

    bt.stop_requested.signal();
    bg_thread.join();

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
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>(::foundation::effects::testing::test().alloc);
    bt.bind_ring(*ring);
    bt.bind_meta_log(*metalog);
    bt.rearm_stop();

    ::fixy::thread::QuiescenceProof<BackgroundThread> const proof = bt.prove_quiescence();
    std::jthread bg_thread([&]() { bt.run_in_row(proof, bg_load_ctx()); });

    constexpr uint32_t TOTAL = 32;
    for (uint32_t i = 0; i < TOTAL; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0xABCD0001ULL + i};
        e.shape_hash = crucible::ShapeHash{0xDEAD0001ULL + i};
        while (!ring->try_append(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bt.total_processed.load_acquire() < TOTAL && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    const uint64_t processed_after_push = bt.total_processed.load_acquire();
    CRUCIBLE_TEST_ASSERT(kTest, processed_after_push >= TOTAL);

    bt.stop_requested.signal();
    bg_thread.join();

    // No signature ever matched twice, so the boundary path never ran and the
    // completed count stays at zero.
    CRUCIBLE_TEST_ASSERT(kTest, bt.iterations_completed.get() == 0u);

    ::fixy::check::pass(kTest,
                        "  large_batch_drain:                         "
                        "PASSED (processed={} of {})\n",
                        static_cast<unsigned long long>(processed_after_push), TOTAL);
}

// Spawn, stop, spawn again.  A once-flag, a static guard inside the template
// instantiation, or a destructor that failed to run would leave the second
// invocation dead, and the second cycle would then drain nothing.
void test_rearm_cycle() {
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>(::foundation::effects::testing::test().alloc);
    bt.bind_ring(*ring);
    bt.bind_meta_log(*metalog);

    constexpr uint32_t PER_CYCLE = 4;

    uint64_t prev_processed = 0;

    for (uint32_t cycle = 0; cycle < 2; ++cycle) {
        bt.rearm_stop();

        ::fixy::thread::QuiescenceProof<BackgroundThread> const proof = bt.prove_quiescence();
        std::jthread bg([&]() { bt.run_in_row(proof, bg_load_ctx()); });

        for (uint32_t i = 0; i < PER_CYCLE; ++i) {
            crucible::TraceRing::Entry e{};
            // The cycle index enters the hash so the detector cannot tie the
            // two cycles together into one signature.
            e.schema_hash = crucible::SchemaHash{0x10000ULL + (cycle << 16) + i};
            e.shape_hash = crucible::ShapeHash{0x20000ULL + i};
            while (
                !ring->try_append(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }

        const uint64_t target = prev_processed + PER_CYCLE;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (bt.total_processed.load_acquire() < target && std::chrono::steady_clock::now() < deadline) {
            CRUCIBLE_SPIN_PAUSE;
        }
        CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= target);

        bt.stop_requested.signal();
        bg.join();

        prev_processed = bt.total_processed.load_acquire();
    }

    // Both invocations consumed entries, so the total covers both cycles.
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() >= 2 * PER_CYCLE);

    ::fixy::check::pass(kTest,
                        "  rearm_cycle:                               "
                        "PASSED (total={} over 2 cycles)\n",
                        static_cast<unsigned long long>(bt.total_processed.load_acquire()));
}

// Two runs of one pipeline at the same time would write each stage from two
// threads.  A run claims the run state, so the second claim ends the process,
// whichever of the two threads claims second.  No stop comes, so the first
// run does not end before the second claim.  The two runs start in a child
// through expect_dies of the Check family, as two bodies of run_joined, and
// each body takes its own proof of quiescence.
void test_second_run_ends_the_process(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] {
            static TraceRing ring{};
            static MetaLog metalog{::foundation::effects::testing::test().alloc};
            BackgroundThread bt{::foundation::effects::testing::test().alloc};
            bt.bind_ring(ring);
            bt.bind_meta_log(metalog);
            bt.rearm_stop();
            auto first = [&bt]() noexcept { bt.run_in_row(bt.prove_quiescence(), bg_load_ctx()); };
            auto second = [&bt]() noexcept { bt.run_in_row(bt.prove_quiescence(), bg_load_ctx()); };
            ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
            static_cast<void>(
                ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second)));
        },
        ::fixy::TextView{"a run of the pipeline while another run of it is active"});
    ::fixy::check::pass(test, "  second_run_ends_the_process:               PASSED\n");
}

}  // namespace test_background_thread_run_in_row
