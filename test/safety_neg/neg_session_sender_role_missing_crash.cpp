// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `assert_every_offer_has_crash_branch_for<Proto, PeerTag>()`
// where Proto contains a Sender<PeerTag>-annotated Offer that DOES NOT
// carry the Recv<Crash<PeerTag>, _> branch.  Per #367 the sender
// annotation tells the walker "this Offer is from PeerTag" — if
// PeerTag matches the queried role, the crash branch becomes
// mandatory and its absence fires the framework-controlled
// [CrashBranch_Missing_In_Tree] diagnostic.
//
// Compare to test_session_sender_role: when the Offer's sender is
// a DIFFERENT role, the walker vacuously accepts absence of the
// crash branch ("not your peer, not your problem").  Here we trigger
// the sender-matches branch of the check.

#include <crucible/sessions/SessionCrash.h>

using namespace crucible::safety::proto;

struct Alice {};
struct Msg {};
struct Ack {};

// Alice-annotated Offer with ONLY non-crash branches.  The walker's
// has_crash_branch_for_peer_v<Offer<Sender<Alice>, ...>, Alice> returns
// false (sender == PeerTag and no Crash<Alice> branch present), so
// every_offer_has_crash_branch_for_peer_v<_, Alice> is false, and the
// consteval assert fires.
using NoAliceCrashProto = Offer<Sender<Alice>,
    Recv<Msg, End>,
    Recv<Ack, End>>;

int main() {
    crucible::safety::proto::assert_every_offer_has_crash_branch_for<
        NoAliceCrashProto, Alice>();
    return 0;
}
