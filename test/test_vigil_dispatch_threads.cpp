// The producer thread gate of test_vigil_dispatch: the ring and the cold
// gates admit only the thread that holds the producer claim.

#include "vigil_dispatch.h"

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>
#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include "test_abort_probe.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace crucible;

namespace test_vigil_dispatch {

// The recording ring is single-producer. Two threads calling dispatch_op
// claim the same slot and the later write erases the earlier one, with no
// diagnostic and no crash — the trace simply comes out short and wrong. A
// backward pass under a foreign runtime dispatches part of its operations on
// a worker thread of its own, so this is reachable from a first real model
// rather than from a later rewrite.
//
// dispatch_op therefore claims the first thread that reaches it and rejects
// every other one, in every build mode. This proves the rejection happens,
// and the same source runs under the release preset where a contract clause
// would have been compiled out.
//
// The rejection ends the process by design, so test::aborts arms a jump on
// the intruder thread and catches it there. The arming is thread-local, which
// is what lets the guard fire on a thread other than the one running main.
void test_second_producer_is_rejected() {
    bool rejected = false;
    {
        Vigil vigil;

        // Claims this thread as the producer.
        auto first = make_op(0, 0);
        auto r0 = crucible::test::dispatch_synthetic(vigil, first.entry, first.metas, first.n_metas);
        assert(r0.action == DispatchResult::Action::RECORD);

        std::thread intruder([&vigil, &rejected] {
            rejected = crucible::test::aborts([&vigil] {
                auto second = make_op(0, 1);
                (void)crucible::test::dispatch_synthetic(vigil, second.entry, second.metas, second.n_metas);
            });
        });
        intruder.join();
    }

    if (!rejected) {
        std::fprintf(stderr, "  test_second_producer_is_rejected: a second thread reached the ring\n");
        std::abort();
    }

    // The same call from the claiming thread keeps working, so the gate is a
    // gate and not a blanket refusal.
    Vigil vigil;
    auto d = make_op(0, 0);
    auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    assert(r.action == DispatchResult::Action::RECORD);
    auto again = make_op(0, 1);
    auto r2 = crucible::test::dispatch_synthetic(vigil, again.entry, again.metas, again.n_metas);
    assert(r2.action == DispatchResult::Action::RECORD);

    crucible::test::pass("  test_second_producer_is_rejected: PASSED\n");
}

// A reference to the producer context can reach another thread, and there
// it passes every type check.  The cold gates therefore also check the
// thread at run time: the mutable view of each table, and the ring and the
// metadata log of the Vigil.  Each one ends the process on a thread that
// does not hold the claim, and admits the thread that holds it.
void test_cold_gates_reject_a_context_on_another_thread() {
    Vigil vigil;
    const VigilFgCtx fg = vigil.mint_producer_context();

    bool is_schema_view_rejected = false;
    bool is_ckernel_view_rejected = false;
    bool is_ring_rejected = false;
    bool is_meta_log_rejected = false;
    std::thread intruder([&] {
        is_schema_view_rejected =
            crucible::test::aborts([&fg] { static_cast<void>(global_schema_table().mint_mutable_view(fg)); });
        is_ckernel_view_rejected =
            crucible::test::aborts([&fg] { static_cast<void>(global_ckernel_table().value()->mint_mutable_view(fg)); });
        is_ring_rejected = crucible::test::aborts([&vigil, &fg] { static_cast<void>(vigil.ring(fg)); });
        is_meta_log_rejected = crucible::test::aborts([&vigil, &fg] { static_cast<void>(vigil.meta_log(fg)); });
    });
    intruder.join();

    assert(is_schema_view_rejected && "the schema table view admitted a thread that holds no claim");
    assert(is_ckernel_view_rejected && "the kernel table view admitted a thread that holds no claim");
    assert(is_ring_rejected && "the ring admitted a thread that holds no claim");
    assert(is_meta_log_rejected && "the metadata log admitted a thread that holds no claim");

    // The claiming thread passes the same gates.
    static_cast<void>(global_schema_table().mint_mutable_view(fg));
    static_cast<void>(global_ckernel_table().value()->mint_mutable_view(fg));
    static_cast<void>(vigil.ring(fg));
    static_cast<void>(vigil.meta_log(fg));

    crucible::test::pass("  test_cold_gates_reject_a_context_on_another_thread: PASSED\n");
}

// The cold gate of a table asks which thread holds the live claims of the
// brand, so one thread at a time holds them.  A second thread that claims
// another Vigil while this thread holds a claim ends the process.
void test_second_thread_cannot_claim_the_brand() {
    bool is_second_claim_rejected = false;
    {
        Vigil first;
        static_cast<void>(first.mint_producer_context());

        std::thread other([&is_second_claim_rejected] {
            Vigil second;
            is_second_claim_rejected =
                crucible::test::aborts([&second] { static_cast<void>(second.mint_producer_context()); });
        });
        other.join();
    }
    assert(is_second_claim_rejected && "a second thread claimed the brand while this thread held a claim");

    // After the first Vigil is gone, another thread claims the brand.
    bool is_later_claim_admitted = false;
    std::thread later_thread([&is_later_claim_admitted] {
        Vigil later;
        static_cast<void>(later.mint_producer_context());
        is_later_claim_admitted = later.is_producer_thread();
    });
    later_thread.join();
    assert(is_later_claim_admitted && "no claim of the brand was live, so another thread must claim it");

    crucible::test::pass("  test_second_thread_cannot_claim_the_brand: PASSED\n");
}

}  // namespace test_vigil_dispatch
