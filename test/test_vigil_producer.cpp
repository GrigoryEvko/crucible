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

// The producer surface of the ring and of the metadata log takes the context
// of the producer claim.  The counters take no context, and they agree with
// the surface that the context opens.
void test_producer_surface_takes_the_claim_context() {
    crucible::Vigil vigil;
    auto first = divrig::op_at(0, 0);
    CRUCIBLE_TEST_ASSERT(
        kTest, vigil.record_op(crucible::test::certify_synthetic_op(first.entry, first.metas, first.n_metas)));

    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    const crucible::TraceRing& ring = vigil.ring(fg);
    const crucible::MetaLog& meta_log = vigil.meta_log(fg);
    CRUCIBLE_TEST_ASSERT(kTest, ring.total_produced() == vigil.ring_total_produced());
    CRUCIBLE_TEST_ASSERT(kTest, ring.total_produced() >= 1 && "the recorded op reached the ring");
    CRUCIBLE_TEST_ASSERT(kTest, meta_log.size().peek() == vigil.meta_log_size().peek());
    CRUCIBLE_TEST_ASSERT(kTest, &vigil.ring(fg) == &ring && "every call opens the same ring");

    ::fixy::check::pass(kTest, "  test_producer_surface_takes_the_claim_context: PASSED\n");
}

// record_op publishes the heads of the ring and of the log with the proof of
// the claim, and the producer surface publishes them with the checked form
// (THE TWO PUBLISHES of crucible/TraceRing.h).  The two forms keep one copy
// of each head on the line of the producer, and the checked publish compares
// the head with that copy.  A proven publish that left the copy behind ends the
// process at the next checked publish, and the count of the ring shows a lost
// or a doubled op.
void test_two_publishes_keep_one_copy_of_the_head() {
    crucible::Vigil vigil;
    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    crucible::TraceRing& ring = vigil.ring(fg);
    crucible::MetaLog& meta_log = vigil.meta_log(fg);
    const uint64_t first_produced = ring.total_produced();

    constexpr uint32_t kRounds = 64;
    for (uint32_t round = 0; round < kRounds; ++round) {
        auto proven = divrig::op_at(round, 0);
        CRUCIBLE_TEST_ASSERT(
            kTest, vigil.record_op(crucible::test::certify_synthetic_op(proven.entry, proven.metas, proven.n_metas)));
        auto checked = divrig::op_at(round, 1);
        const crucible::MetaIndex meta_start =
            meta_log.try_append(crucible::test::records_of(checked.metas, checked.n_metas));
        CRUCIBLE_TEST_ASSERT(kTest, meta_start.is_valid() && "the log has room for the records of the checked append");
        CRUCIBLE_TEST_ASSERT(kTest,
                             ring.try_append(checked.entry, meta_start) && "the ring has room for the checked append");
    }

    CRUCIBLE_TEST_ASSERT(kTest, ring.total_produced() == first_produced + 2 * kRounds
                                    && "each proven and each checked publish moved the head by one op");
    ::fixy::check::pass(kTest, "  test_two_publishes_keep_one_copy_of_the_head: PASSED\n");
}

// The foreground reads the count of iterations while the detect stage writes
// it.  After the flush, the test records two more regions and reads the count
// until it moves, with no other synchronization with the pipeline.  A count
// that is not atomic gives a data race here, and ThreadSanitizer reports it.
void test_foreground_reads_the_iteration_count() {
    crucible::Vigil vigil;
    divrig::feed_one_region(vigil, 0);
    vigil.flush();
    const uint32_t after_flush = vigil.bg_iterations_completed();
    CRUCIBLE_TEST_ASSERT(kTest, after_flush > 0 && "one region needs at least one iteration");

    divrig::feed_one_region(vigil, 4);
    divrig::feed_one_region(vigil, 8);
    while (vigil.bg_iterations_completed() <= after_flush) {
        CRUCIBLE_SPIN_PAUSE;
    }

    vigil.flush();
    const uint32_t after_all = vigil.bg_iterations_completed();
    CRUCIBLE_TEST_ASSERT(kTest, after_all > after_flush && "the two regions after the flush add iterations");

    ::fixy::check::pass(kTest,
                        "  test_foreground_reads_the_iteration_count: PASSED (after the flush {}, at the end {})\n",
                        after_flush, after_all);
}

}  // namespace test_vigil
