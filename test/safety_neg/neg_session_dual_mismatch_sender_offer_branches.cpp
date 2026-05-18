// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-022: companion fixture to
// neg_session_dual_mismatch_sender_offer_reversed.cpp.  Exercises the
// symmetric-`is_dual_v` discipline against a DIFFERENT mismatch class
// — element-wise BRANCH type drift inside a Sender-annotated Offer
// pair.  The shapes line up (one branch each, Sender-Offer on one
// side and Select on the other) but the BRANCH PAYLOADS are duals of
// DIFFERENT messages.  Neither `dual_of_t<P1> == P2` nor
// `P1 == dual_of_t<P2>` can hold.
//
// This is the load-bearing regression test for the OR-disjunction
// form of `is_dual_v`: a wrongly-permissive symmetric definition
// (e.g. accepting any pair where ONE shape happens to match
// structurally without branch-type drilldown) would FAIL to fire
// here.  The fix as shipped at sessions/Session.h:756 stays
// branch-type-strict because the underlying `std::is_same_v` requires
// full structural equality including the recursive branch types.
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "Dual_Mismatch"  |
//   "ensure_dual"

#include <crucible/sessions/Session.h>

namespace proto = ::crucible::safety::proto;

struct BobRole {};
struct RealMessage {};
struct StrayMessage {};  // unrelated payload type

// Receiver-side: offers a single branch carrying RealMessage payload.
using ServerProto =
    proto::Offer<proto::Sender<BobRole>,
                 proto::Recv<RealMessage, proto::End>>;

// Sender-side: shape matches (one branch, Select-flavoured) but
// types the payload as StrayMessage, not RealMessage.
// `dual_of_t<ServerProto>` = `Select<Send<RealMessage, End>>`,
// which is NOT equal to `Select<Send<StrayMessage, End>>`.  The
// reverse orientation also fails for the same payload-type reason.
using ClientProto =
    proto::Select<proto::Send<StrayMessage, proto::End>>;

void compile_time_reject() {
    proto::ensure_dual<ClientProto, ServerProto>();
}

int main() { return 0; }
