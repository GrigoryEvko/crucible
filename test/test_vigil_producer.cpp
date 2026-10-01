// The producer role of test_vigil, and the producer surface that the
// context of the claim opens.

#include "vigil_rig.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdio>
#include <thread>

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
    assert(vigil.is_producer_thread() && "an unclaimed Vigil admits the first thread that arrives");

    std::thread claimer([&vigil] {
        auto first = divrig::op_at(0, 0);
        assert(vigil.record_op(crucible::test::certify_synthetic_entry(first.entry), first.metas, first.n_metas));
        assert(vigil.is_producer_thread() && "the thread that claimed the role holds it");
    });
    claimer.join();

    assert(!vigil.is_producer_thread()
           && "record_op must claim the producer role, which makes every later thread a second producer");

    std::printf("  test_record_op_claims_the_producer_role: PASSED\n");
}

// The producer surface of the ring and of the metadata log takes the context
// of the producer claim.  The counters take no context, and they agree with
// the surface that the context opens.
void test_producer_surface_takes_the_claim_context() {
    crucible::Vigil vigil;
    auto first = divrig::op_at(0, 0);
    assert(vigil.record_op(crucible::test::certify_synthetic_entry(first.entry), first.metas, first.n_metas));

    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    const crucible::TraceRing& ring = vigil.ring(fg);
    const crucible::MetaLog& meta_log = vigil.meta_log(fg);
    assert(ring.total_produced() == vigil.ring_total_produced());
    assert(ring.total_produced() >= 1 && "the recorded op reached the ring");
    assert(meta_log.size().peek() == vigil.meta_log_size().peek());
    assert(&vigil.ring(fg) == &ring && "every call opens the same ring");

    std::printf("  test_producer_surface_takes_the_claim_context: PASSED\n");
}

}  // namespace test_vigil
