// The producer thread gate of test_vigil_dispatch: the ring and the cold
// gates admit only the thread that holds the producer claim.

#include "vigil_dispatch.h"

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"
#include "test_assert.h"
#include <thread>

using namespace crucible;

namespace test_vigil_dispatch {

namespace {

// The text of the claim check of a thread that holds no producer claim.
constexpr ::fixy::TextView kNotTheHolder{"foundation: fatal invariant failed: holder == current_tid"};

}  // namespace

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
// The rejection ends the process by design, so each case runs in a child
// through expect_dies of the Check family.  The child builds the Vigil,
// claims it on its first thread, and starts the intruder thread.  The test
// process has no second thread at the fork, so the child may start threads.
void test_second_producer_is_rejected(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            Vigil vigil;

            // Claims this thread as the producer.
            auto first = make_op(0, 0);
            static_cast<void>(crucible::test::dispatch_synthetic(vigil, first.entry, first.metas, first.n_metas));

            std::thread intruder([&vigil] {
                auto second = make_op(0, 1);
                (void)crucible::test::dispatch_synthetic(vigil, second.entry, second.metas, second.n_metas);
            });
            intruder.join();
        },
        kNotTheHolder);

    // The same call from the claiming thread keeps working, so the gate is a
    // gate and not a blanket refusal.
    Vigil vigil;
    auto d = make_op(0, 0);
    auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
    CRUCIBLE_TEST_ASSERT(test, r.action == DispatchResult::Action::RECORD);
    auto again = make_op(0, 1);
    auto r2 = crucible::test::dispatch_synthetic(vigil, again.entry, again.metas, again.n_metas);
    CRUCIBLE_TEST_ASSERT(test, r2.action == DispatchResult::Action::RECORD);

    ::fixy::check::pass(test, "  test_second_producer_is_rejected: PASSED\n");
}

// A reference to the producer context can reach another thread, and there
// it passes every type check.  The cold gates therefore also check the
// thread at run time: the mutable view of each table, and the ring and the
// metadata log of the Vigil.  Each one ends the process on a thread that
// does not hold the claim, and admits the thread that holds it.  The claim
// of the Vigil also refuses a second claim from the other thread.
//
// Each refusal runs in its own child: the child builds a Vigil, claims it,
// and calls the gate on an intruder thread with the context of the claim.
template <typename Gate>
void expect_gate_refuses_the_intruder(::fixy::TestRunnerCtx const& test, Gate gate, ::fixy::TextView refusal) {
    ::fixy::check::expect_dies(
        test,
        [gate] noexcept {
            Vigil vigil;
            const VigilFgCtx fg = vigil.mint_producer_context();
            std::thread intruder([&vigil, &fg, gate] { gate(vigil, fg); });
            intruder.join();
        },
        refusal);
}

void test_cold_gates_reject_a_context_on_another_thread(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kNotTheBrandThread{
        "foundation: fatal invariant failed: ProducerClaim<Brand>::can_caller_use_brand()"};
    constexpr ::fixy::TextView kNotTheVigilThread{
        "foundation: fatal invariant failed: producer_claim_.is_claimable_by_caller()"};
    expect_gate_refuses_the_intruder(
        test, [](Vigil& vigil, VigilFgCtx const&) noexcept { static_cast<void>(vigil.mint_producer_context()); },
        kNotTheHolder);
    expect_gate_refuses_the_intruder(
        test,
        [](Vigil&, VigilFgCtx const& fg) noexcept { static_cast<void>(global_schema_table().mint_mutable_view(fg)); },
        kNotTheBrandThread);
    expect_gate_refuses_the_intruder(
        test,
        [](Vigil&, VigilFgCtx const& fg) noexcept {
            static_cast<void>(global_ckernel_table().value()->mint_mutable_view(fg));
        },
        kNotTheBrandThread);
    expect_gate_refuses_the_intruder(
        test, [](Vigil& vigil, VigilFgCtx const& fg) noexcept { static_cast<void>(vigil.ring(fg)); },
        kNotTheVigilThread);
    expect_gate_refuses_the_intruder(
        test, [](Vigil& vigil, VigilFgCtx const& fg) noexcept { static_cast<void>(vigil.meta_log(fg)); },
        kNotTheVigilThread);

    // The claiming thread passes the same gates.
    Vigil vigil;
    const VigilFgCtx fg = vigil.mint_producer_context();
    static_cast<void>(global_schema_table().mint_mutable_view(fg));
    static_cast<void>(global_ckernel_table().value()->mint_mutable_view(fg));
    static_cast<void>(vigil.ring(fg));
    static_cast<void>(vigil.meta_log(fg));

    ::fixy::check::pass(test, "  test_cold_gates_reject_a_context_on_another_thread: PASSED\n");
}

// A brand of this test.  Its claim obeys the rule of each brand, and the
// Vigil holds the same claim for its own brand.  Two live claims of this
// brand hold no metadata log, and two live Vigils hold two logs of 176 MB.
struct ClaimBrand {
    ::foundation::effects::host::ProducerClaim<ClaimBrand> claim;
};

// The cold gate of a table asks which thread holds the live claims of the
// brand, so one thread at a time holds them.  A second thread that claims
// another object of the brand while this thread holds a claim ends the
// process.  The two claims run in a child through expect_dies.
void test_second_thread_cannot_claim_the_brand(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            ClaimBrand first;
            static_cast<void>(first.claim.mint_producer_context());

            std::thread other([] {
                ClaimBrand second;
                static_cast<void>(second.claim.mint_producer_context());
            });
            other.join();
        },
        ::fixy::TextView{"foundation: fatal invariant failed: record != nullptr"});

    // The Vigils of the cases above are gone, so another thread claims the
    // brand of the Vigil.
    bool is_later_claim_admitted = false;
    std::thread later_thread([&is_later_claim_admitted] {
        Vigil later;
        static_cast<void>(later.mint_producer_context());
        is_later_claim_admitted = later.is_producer_thread();
    });
    later_thread.join();
    CRUCIBLE_TEST_ASSERT(test,
                         is_later_claim_admitted && "no claim of the brand was live, so another thread must claim it");

    ::fixy::check::pass(test, "  test_second_thread_cannot_claim_the_brand: PASSED\n");
}

// A claim that its thread gives up (release_claim) leaves the record of the
// brand, so a second thread claims the brand while the first object lives,
// and it claims that object itself too.  A release of a claim of another
// thread ends the process, and a release with no claim changes nothing.
void test_released_claim_lets_another_thread_claim(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            ClaimBrand first;
            static_cast<void>(first.claim.mint_producer_context());
            std::thread other([&first] { first.claim.release_claim(); });
            other.join();
        },
        ::fixy::TextView{
            "foundation: fatal invariant failed: holder == detail::producer_claim::calling_thread_identity()"});

    ClaimBrand first;
    first.claim.release_claim();
    CRUCIBLE_TEST_ASSERT(test, first.claim.is_claimable_by_caller() && "a release with no claim changes nothing");
    static_cast<void>(first.claim.mint_producer_context());
    first.claim.release_claim();
    bool is_brand_claimed = false;
    bool is_first_claimed = false;
    std::thread other([&first, &is_brand_claimed, &is_first_claimed] {
        ClaimBrand second;
        static_cast<void>(second.claim.mint_producer_context());
        is_brand_claimed = second.claim.is_claimable_by_caller();
        static_cast<void>(first.claim.mint_producer_context());
        is_first_claimed = first.claim.is_claimable_by_caller();
        first.claim.release_claim();
    });
    other.join();
    CRUCIBLE_TEST_ASSERT(test, is_brand_claimed && is_first_claimed && "the released claim leaves the brand free");
    CRUCIBLE_TEST_ASSERT(test, first.claim.is_claimable_by_caller() && "the other thread gave the claim up again");

    // A Vigil gives up its claim in the same way: a thread that did not create
    // it records into it after the release.
    Vigil vigil;
    auto op = make_op(0, 0);
    static_cast<void>(crucible::test::dispatch_synthetic(vigil, op.entry, op.metas, op.n_metas));
    vigil.release_producer_claim();
    DispatchResult::Action action = DispatchResult::Action::COMPILED;
    std::thread recorder([&vigil, &action] {
        auto next = make_op(0, 1);
        action = crucible::test::dispatch_synthetic(vigil, next.entry, next.metas, next.n_metas).action;
        vigil.release_producer_claim();
    });
    recorder.join();
    CRUCIBLE_TEST_ASSERT(test, action == DispatchResult::Action::RECORD);

    ::fixy::check::pass(test, "  test_released_claim_lets_another_thread_claim: PASSED\n");
}

}  // namespace test_vigil_dispatch
