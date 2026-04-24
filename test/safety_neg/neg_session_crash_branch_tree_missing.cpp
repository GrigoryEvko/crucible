// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `assert_every_offer_has_crash_branch_for<P, Peer>()`
// invoked on a protocol tree where a DEEP Offer lacks the crash
// branch — even though the outermost Offer has one.  Per #368, the
// aggregate walker fires the framework-controlled
// "[CrashBranch_Missing_In_Tree]" diagnostic.

#include <crucible/safety/SessionCrash.h>

using namespace crucible::safety::proto;

struct UnreliablePeer {};
struct Msg {};

// Inner Offer has no Crash<UnreliablePeer, _> branch.
using InnerBadOffer = Offer<Recv<Msg, End>>;

// Outer Offer has the crash branch locally, but the non-crash
// branch's continuation hides the bad inner Offer.  A one-Offer
// `has_crash_branch_for_peer_v` check on the outer would PASS;
// only the walker catches the deep lack.
using OuterHidingBad = Offer<
    Recv<Msg, InnerBadOffer>,
    Recv<Crash<UnreliablePeer>, End>>;

void compile_time_reject() {
    assert_every_offer_has_crash_branch_for<OuterHidingBad, UnreliablePeer>();
}

int main() {
    return 0;
}
