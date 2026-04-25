// Runtime + compile-time harness for the protocol-tree crash-branch
// walker added in #368 (SEPLOG-BUG-4): every_offer_has_crash_branch_for_peer_v
// and assert_every_offer_has_crash_branch_for<Proto, PeerTag>().
//
// Coverage:
//   * Compile-time: the walker recurses through every local-protocol
//     combinator head (End / Stop / Continue / Send / Recv / Select /
//     Offer / Loop / Delegate / Accept) with positive and negative
//     cases per shape.  Critically: a tree that is locally-OK at the
//     first Offer but has a deeper Offer with no crash branch MUST
//     fail the aggregate predicate (the shortcut that #368 addresses).
//   * Structural witness: the consteval assertion fires the framework-
//     controlled "[CrashBranch_Missing_In_Tree]" diagnostic at the
//     call site (not buried in a template instantiation tree).
//
// The test is pure compile-time; main() just prints a confirmation
// string so the test harness records "PASSED".

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <cstdio>

namespace {

using namespace crucible::safety::proto;

// ── Fixtures ─────────────────────────────────────────────────────

struct UnreliablePeer {};
struct ReliablePeer   {};
struct Msg {};
struct Ping {};
struct Pong {};

using Rec = Recv<Crash<UnreliablePeer>, End>;     // recovery body

// ── 1. Terminals and atoms ───────────────────────────────────────

// End / Stop / Continue have no Offers — trivially all-covered for
// any peer (vacuous truth).
static_assert(every_offer_has_crash_branch_for_peer_v<End,      UnreliablePeer>);
static_assert(every_offer_has_crash_branch_for_peer_v<Stop,     UnreliablePeer>);
static_assert(every_offer_has_crash_branch_for_peer_v<Continue, UnreliablePeer>);

// Send / Recv without any Offer — still vacuously true.
static_assert(every_offer_has_crash_branch_for_peer_v<
    Send<Msg, End>, UnreliablePeer>);
static_assert(every_offer_has_crash_branch_for_peer_v<
    Recv<Msg, Send<Msg, End>>, UnreliablePeer>);

// ── 2. Single Offer — positive case ───────────────────────────────

// An Offer with a Recv<Crash<Peer>, _> branch passes for that peer.
using OfferWithCrash = Offer<Recv<Ping, End>, Recv<Crash<UnreliablePeer>, End>>;
static_assert(every_offer_has_crash_branch_for_peer_v<OfferWithCrash, UnreliablePeer>);

// But DOES NOT pass for a different peer — the same check against
// ReliablePeer fails because the Offer has no Recv<Crash<ReliablePeer>, _>.
// (In practice, ReliablePeer would be in ReliableSet<> and skipped
// at the aggregate-check level; the walker itself is per-peer.)
static_assert(!every_offer_has_crash_branch_for_peer_v<OfferWithCrash, ReliablePeer>);

// ── 3. Single Offer — negative case ───────────────────────────────

using OfferWithoutCrash = Offer<Recv<Ping, End>, Recv<Pong, End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<OfferWithoutCrash, UnreliablePeer>);

// ── 4. Nested Offer within a branch's continuation ─────────────────
//
// The "gotcha" case: the outer Offer has a crash branch, but a
// non-crash branch's continuation contains ANOTHER Offer that
// lacks one.  The aggregate predicate must catch this.

using InnerBadOffer = Offer<Recv<Pong, End>>;     // no crash branch
using OuterWithBadInner = Offer<
    Recv<Ping, InnerBadOffer>,                    // branch continuation has a bad inner Offer
    Recv<Crash<UnreliablePeer>, End>>;
// Outer HAS a crash branch → has_crash_branch_for_peer_v is true on outer.
static_assert( has_crash_branch_for_peer_v<OuterWithBadInner, UnreliablePeer>);
// But the aggregate walker DESCENDS and sees InnerBadOffer's lack.
static_assert(!every_offer_has_crash_branch_for_peer_v<OuterWithBadInner, UnreliablePeer>);

// The "fix" — inner Offer also gets a crash branch.
using InnerGoodOffer = Offer<Recv<Pong, End>, Recv<Crash<UnreliablePeer>, End>>;
using OuterWithGoodInner = Offer<
    Recv<Ping, InnerGoodOffer>,
    Recv<Crash<UnreliablePeer>, End>>;
static_assert( every_offer_has_crash_branch_for_peer_v<OuterWithGoodInner, UnreliablePeer>);

// ── 5. Select does NOT demand a crash branch — the choice is OURS ──
//
// Select<Bs...> is INTERNAL choice; branches are our Sends.  The peer
// can't crash during a Select (no Recv happens).  The walker recurses
// into each branch but doesn't require a Crash branch on Select itself.

using SelectOverCrashOffers = Select<
    Send<Ping, InnerGoodOffer>,         // branch 0: send, then bad/good-less inner
    Send<Pong, End>>;
static_assert( every_offer_has_crash_branch_for_peer_v<SelectOverCrashOffers, UnreliablePeer>);

// But if Select's branch contains a bad inner Offer, the walker fails.
using SelectOverBadOffer = Select<
    Send<Ping, InnerBadOffer>,
    Send<Pong, End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<SelectOverBadOffer, UnreliablePeer>);

// ── 6. Loop — walker recurses into body ─────────────────────────────

// Loop body contains a crash-safe Offer — passes.
using LoopCrashSafe = Loop<Offer<Recv<Ping, Continue>, Recv<Crash<UnreliablePeer>, End>>>;
static_assert( every_offer_has_crash_branch_for_peer_v<LoopCrashSafe, UnreliablePeer>);

// Loop body Offer missing the crash branch — fails.
using LoopCrashUnsafe = Loop<Offer<Recv<Ping, Continue>>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<LoopCrashUnsafe, UnreliablePeer>);

// ── 7. Delegate / Accept — walker recurses into K only, not T ──────
//
// Delegated protocol T is executed by the recipient; walker skips it.
// Continuation K is executed by us; walker recurses into it.

// Delegate with a crash-safe continuation — passes.
using DelegateCrashSafe = Delegate<Recv<Msg, End>, OfferWithCrash>;
static_assert( every_offer_has_crash_branch_for_peer_v<DelegateCrashSafe, UnreliablePeer>);

// Delegate with a crash-unsafe continuation — fails.
using DelegateCrashUnsafe = Delegate<Recv<Msg, End>, OfferWithoutCrash>;
static_assert(!every_offer_has_crash_branch_for_peer_v<DelegateCrashUnsafe, UnreliablePeer>);

// Notably: the DELEGATED protocol T (the `Recv<Msg, End>` above) is
// deliberately SKIPPED by the walker — even if T contained a bad
// Offer, the walker of the LOCAL protocol doesn't flag it.
using TWithBadOffer = OfferWithoutCrash;                   // bad at T
using DelegateSkipsT = Delegate<TWithBadOffer, End>;       // K is End (safe)
static_assert( every_offer_has_crash_branch_for_peer_v<DelegateSkipsT, UnreliablePeer>);

// Accept: same recursion pattern.
using AcceptCrashSafe = Accept<Recv<Msg, End>, OfferWithCrash>;
using AcceptCrashUnsafe = Accept<Recv<Msg, End>, OfferWithoutCrash>;
static_assert( every_offer_has_crash_branch_for_peer_v<AcceptCrashSafe,   UnreliablePeer>);
static_assert(!every_offer_has_crash_branch_for_peer_v<AcceptCrashUnsafe, UnreliablePeer>);

// ── 8. Deeply nested mixed shape — stress test ────────────────────

using DeeplyNestedSafe = Loop<Select<
    Send<Ping, OfferWithCrash>,                            // crash-safe Offer in Select branch
    Send<Pong, Loop<Recv<Msg, OfferWithCrash>>>,           // nested Loop + Recv + safe Offer
    End>>;
static_assert( every_offer_has_crash_branch_for_peer_v<DeeplyNestedSafe, UnreliablePeer>);

using DeeplyNestedUnsafe = Loop<Select<
    Send<Ping, OfferWithCrash>,
    Send<Pong, Loop<Recv<Msg, OfferWithoutCrash>>>,        // ← bad inner Offer
    End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<DeeplyNestedUnsafe, UnreliablePeer>);

// ── 9. Consteval assertion fires at the call site ──────────────────
//
// assert_every_offer_has_crash_branch_for<Proto, PeerTag>() is a
// one-line discipline tool.  For a crash-safe proto it compiles
// silently; for a crash-unsafe one it fires the
// [CrashBranch_Missing_In_Tree] diagnostic.

consteval void compile_time_check() {
    assert_every_offer_has_crash_branch_for<LoopCrashSafe,    UnreliablePeer>();
    assert_every_offer_has_crash_branch_for<DeeplyNestedSafe, UnreliablePeer>();
    assert_every_offer_has_crash_branch_for<OfferWithCrash,   UnreliablePeer>();
    // Intentionally NOT asserting on the Unsafe variants — the neg-
    // compile test below exercises that rejection path.
}

}  // anonymous namespace

int main() {
    compile_time_check();
    std::puts("session_crash_walker: every-offer-has-crash-branch walker + "
              "Delegate/Accept recursion + nested-shape coverage OK");
    return 0;
}
