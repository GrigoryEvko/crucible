// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assert_has_crash_branch_for<Offer, UnreliablePeer>()
// where the Offer has no Recv<Crash<UnreliablePeer>, _> branch.
// L8 SessionCrash.h's assertion helper fires.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>

using namespace crucible::safety::proto;

struct UnreliableServer {};
struct Request  {};
struct Response {};

// Offer has no crash branch for UnreliableServer — unsafe for an
// unreliable peer.
using UnsafeOffer = Offer<
    Recv<Request,  End>,
    Recv<Response, End>>;

int main() {
    assert_has_crash_branch_for<UnsafeOffer, UnreliableServer>();
    return 0;
}
