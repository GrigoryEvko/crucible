// The producer role of test_vigil, and the producer surface that the
// context of the claim opens.

#include "vigil_rig.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstdio>
#include <thread>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace test_vigil {

// The ring behind Vigil is single-producer.  record_op reaches it without
// going through dispatch_op, so it carries the same thread gate, and the
// first thread through it claims the producer role.
//
// Proving the claim happens proves the gate is on this entry point, and it
// needs no abort to observe: the claiming thread is joined before the role
// is read back, so the read carries a happens-before edge and the recorded
// op stream stays single-threaded throughout.
void test_record_op_claims_the_producer_role() {
    crucible::Vigil vigil;
    CRUCIBLE_TEST_ASSERT(kTest,
                         vigil.is_producer_thread() && "an unclaimed Vigil admits the first thread that arrives");

    std::thread claimer([&vigil] {
        auto first = divrig::op_at(0, 0);
        CRUCIBLE_TEST_ASSERT(
            kTest, vigil.record_op(crucible::test::certify_synthetic_op(first.entry, first.metas, first.n_metas)));
        CRUCIBLE_TEST_ASSERT(kTest, vigil.is_producer_thread() && "the thread that claimed the role holds it");
    });
    claimer.join();

    CRUCIBLE_TEST_ASSERT(
        kTest, !vigil.is_producer_thread()
                   && "record_op must claim the producer role, which makes every later thread a second producer");

    ::fixy::check::pass(kTest, "  test_record_op_claims_the_producer_role: PASSED\n");
}

// The counters of the ring and of the metadata log take no context, and they
// count the op and the records that record_op put into them.
void test_counters_count_the_recorded_op() {
    crucible::Vigil vigil;
    const uint64_t ring_before = vigil.ring_total_produced();
    const uint64_t log_before = vigil.meta_log_total_produced().raw();
    auto first = divrig::op_at(0, 0);
    CRUCIBLE_TEST_ASSERT(
        kTest, vigil.record_op(crucible::test::certify_synthetic_op(first.entry, first.metas, first.n_metas)));

    CRUCIBLE_TEST_ASSERT(kTest, vigil.ring_total_produced() == ring_before + 1 && "the recorded op reached the ring");
    CRUCIBLE_TEST_ASSERT(kTest, vigil.meta_log_total_produced().raw() == log_before + first.n_metas
                                    && "the records of the op reached the log");

    ::fixy::check::pass(kTest, "  test_counters_count_the_recorded_op: PASSED\n");
}

// record_op publishes the heads of the ring and of the log through the
// producer ends of the two held splits (the two publishes of
// crucible/TraceRing.h).  Each end keeps one copy of its head on the line of
// the producer, and the step check of each publish compares the new head with
// that copy.  A publish that left the copy behind ends the process at the next
// publish, and the count of the ring shows a lost or a doubled op.
void test_each_record_moves_the_head_by_one_op() {
    crucible::Vigil vigil;
    const uint64_t first_produced = vigil.ring_total_produced();

    constexpr uint32_t kRounds = 64;
    for (uint32_t round = 0; round < kRounds; ++round) {
        for (uint32_t op = 0; op < 2; ++op) {
            auto recorded = divrig::op_at(round, op);
            CRUCIBLE_TEST_ASSERT(kTest, vigil.record_op(crucible::test::certify_synthetic_op(
                                            recorded.entry, recorded.metas, recorded.n_metas)));
        }
    }

    CRUCIBLE_TEST_ASSERT(kTest, vigil.ring_total_produced() == first_produced + 2 * kRounds
                                    && "each publish moved the head by one op");
    ::fixy::check::pass(kTest, "  test_each_record_moves_the_head_by_one_op: PASSED\n");
}

// The foreground reads the count of iterations while the detect stage writes
// it.  After the flush, the test records two more regions and reads the count
// until it moves, with no other synchronization with the pipeline.  A count
// that is not atomic gives a data race here, and ThreadSanitizer reports it.
void test_foreground_reads_the_iteration_count() {
    crucible::Vigil vigil;
    divrig::feed_one_region(vigil, 0);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
    const uint32_t after_flush = vigil.bg_iterations_completed();
    CRUCIBLE_TEST_ASSERT(kTest, after_flush > 0 && "one region needs at least one iteration");

    divrig::feed_one_region(vigil, 4);
    divrig::feed_one_region(vigil, 8);
    while (vigil.bg_iterations_completed() <= after_flush) {
        CRUCIBLE_SPIN_PAUSE;
    }

    CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
    const uint32_t after_all = vigil.bg_iterations_completed();
    CRUCIBLE_TEST_ASSERT(kTest, after_all > after_flush && "the two regions after the flush add iterations");

    ::fixy::check::pass(kTest,
                        "  test_foreground_reads_the_iteration_count: PASSED (after the flush {}, at the end {})\n",
                        after_flush, after_all);
}

}  // namespace test_vigil
