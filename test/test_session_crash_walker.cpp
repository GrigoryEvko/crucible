// Every claim here is a static_assert.  main() only prints, so that the
// harness records a pass.

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <cstdio>

namespace {

using namespace crucible::safety::proto;

struct UnreliablePeer {};
struct ReliablePeer {};
struct Msg {};
struct Ping {};
struct Pong {};

using Rec = Recv<Crash<UnreliablePeer>, End>;

// End, Stop and Continue contain no Offer, so the predicate holds
// vacuously for any peer.
static_assert(every_offer_has_crash_branch_for_peer_v<End, UnreliablePeer>);
static_assert(every_offer_has_crash_branch_for_peer_v<Stop, UnreliablePeer>);
static_assert(every_offer_has_crash_branch_for_peer_v<Continue, UnreliablePeer>);

static_assert(every_offer_has_crash_branch_for_peer_v<Send<Msg, End>, UnreliablePeer>);
static_assert(every_offer_has_crash_branch_for_peer_v<Recv<Msg, Send<Msg, End>>, UnreliablePeer>);

using OfferWithCrash = Offer<Recv<Ping, End>, Recv<Crash<UnreliablePeer>, End>>;
static_assert(every_offer_has_crash_branch_for_peer_v<OfferWithCrash, UnreliablePeer>);

// The walker is per-peer, so the same Offer fails for a peer it has no
// crash branch for.  A peer declared reliable is skipped one level up,
// at the aggregate check, rather than here.
static_assert(!every_offer_has_crash_branch_for_peer_v<OfferWithCrash, ReliablePeer>);

using OfferWithoutCrash = Offer<Recv<Ping, End>, Recv<Pong, End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<OfferWithoutCrash, UnreliablePeer>);

// The case a shallow check misses: the outer Offer has a crash branch,
// but the continuation of one of its other branches holds a second
// Offer that has none.  The aggregate predicate must descend and catch
// that.

using InnerBadOffer = Offer<Recv<Pong, End>>;  // no crash branch
using OuterWithBadInner = Offer<Recv<Ping, InnerBadOffer>, Recv<Crash<UnreliablePeer>, End>>;
// The shallow predicate sees the outer crash branch and is satisfied.
static_assert(has_crash_branch_for_peer_v<OuterWithBadInner, UnreliablePeer>);
// The walking predicate descends and sees what the inner Offer lacks.
static_assert(!every_offer_has_crash_branch_for_peer_v<OuterWithBadInner, UnreliablePeer>);

// The same tree with the inner Offer repaired.
using InnerGoodOffer = Offer<Recv<Pong, End>, Recv<Crash<UnreliablePeer>, End>>;
using OuterWithGoodInner = Offer<Recv<Ping, InnerGoodOffer>, Recv<Crash<UnreliablePeer>, End>>;
static_assert(every_offer_has_crash_branch_for_peer_v<OuterWithGoodInner, UnreliablePeer>);

// Select is an internal choice whose branches are our own sends.  No
// receive happens at a Select, so there is no point for the peer to
// crash at, and the walker requires no crash branch on the Select
// itself.  It still recurses into each branch.

using SelectOverCrashOffers = Select<Send<Ping, InnerGoodOffer>, Send<Pong, End>>;
static_assert(every_offer_has_crash_branch_for_peer_v<SelectOverCrashOffers, UnreliablePeer>);

using SelectOverBadOffer = Select<Send<Ping, InnerBadOffer>, Send<Pong, End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<SelectOverBadOffer, UnreliablePeer>);

using LoopCrashSafe = Loop<Offer<Recv<Ping, Continue>, Recv<Crash<UnreliablePeer>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<LoopCrashSafe, UnreliablePeer>);

using LoopCrashUnsafe = Loop<Offer<Recv<Ping, Continue>>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<LoopCrashUnsafe, UnreliablePeer>);

// We run the continuation ourselves, so the walker recurses into it.
// Delegate also decides whether a recipient crash must propagate: a
// receive-only delegated protocol can be abandoned cleanly, while an
// outbound one demands an immediate recovery branch in the continuation.

using DelegateCrashSafe = Delegate<Recv<Msg, End>, OfferWithCrash>;
static_assert(every_offer_has_crash_branch_for_peer_v<DelegateCrashSafe, UnreliablePeer>);

using DelegateCrashUnsafe = Delegate<Recv<Msg, End>, OfferWithoutCrash>;
static_assert(!every_offer_has_crash_branch_for_peer_v<DelegateCrashUnsafe, UnreliablePeer>);

// The delegated protocol is not walked as our own.  An Offer inside it
// belongs to the recipient's endpoint, and whether the recipient's
// failure reaches us is the separate question the classifier answers.
using TWithBadOffer = OfferWithoutCrash;  // the defect sits here
using DelegateSkipsT = Delegate<TWithBadOffer, End>;  // and our continuation is safe
static_assert(every_offer_has_crash_branch_for_peer_v<DelegateSkipsT, UnreliablePeer>);

// Once the delegated recipient can emit before finishing, the carrier
// continuation must expose a recovery branch for it.
using DelegateRecipientCrashRecovered = Delegate<Send<Msg, End>, OfferWithCrash>;
static_assert(every_offer_has_crash_branch_for_peer_v<DelegateRecipientCrashRecovered, UnreliablePeer>);

using DelegateRecipientCrashUnrecovered = Delegate<Send<Msg, End>, End>;
static_assert(!every_offer_has_crash_branch_for_peer_v<DelegateRecipientCrashUnrecovered, UnreliablePeer>);

// Accept recurses the same way.
using AcceptCrashSafe = Accept<Recv<Msg, End>, OfferWithCrash>;
using AcceptCrashUnsafe = Accept<Recv<Msg, End>, OfferWithoutCrash>;
static_assert(every_offer_has_crash_branch_for_peer_v<AcceptCrashSafe, UnreliablePeer>);
static_assert(!every_offer_has_crash_branch_for_peer_v<AcceptCrashUnsafe, UnreliablePeer>);

using DeeplyNestedSafe = Loop<Select<Send<Ping, OfferWithCrash>, Send<Pong, Loop<Recv<Msg, OfferWithCrash>>>, End>>;
static_assert(every_offer_has_crash_branch_for_peer_v<DeeplyNestedSafe, UnreliablePeer>);

using DeeplyNestedUnsafe = Loop<
    Select<Send<Ping, OfferWithCrash>, Send<Pong, Loop<Recv<Msg, OfferWithoutCrash>>>,  // the only line that differs
           End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<DeeplyNestedUnsafe, UnreliablePeer>);

// The assertion compiles silently for a crash-safe protocol and fires
// the [CrashBranch_Missing_In_Tree] diagnostic at the call site for an
// unsafe one, rather than deep inside an instantiation tree.

consteval void compile_time_check() {
    assert_every_offer_has_crash_branch_for<LoopCrashSafe, UnreliablePeer>();
    assert_every_offer_has_crash_branch_for<DeeplyNestedSafe, UnreliablePeer>();
    assert_every_offer_has_crash_branch_for<OfferWithCrash, UnreliablePeer>();
    // The unsafe variants are deliberately absent.  Naming one here
    // would fail the build, which is the behaviour a negative-compile
    // fixture exercises instead.
}

}  // anonymous namespace

int main() {
    compile_time_check();
    std::puts("session_crash_walker: every-offer-has-crash-branch walker + "
              "Delegate/Accept recursion + nested-shape coverage OK");
    return 0;
}
