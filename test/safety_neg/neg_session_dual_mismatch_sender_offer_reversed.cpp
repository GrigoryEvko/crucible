// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-022: witness that `ensure_dual` still REJECTS genuinely
// non-dual pairs in the REVERSE argument orientation under the new
// symmetric `is_dual_v` definition (the OR over both
// `dual_of_t<P1> == P2` and `P1 == dual_of_t<P2>`).
//
// Setup: `OfferS` is a Sender-annotated Offer whose `dual_of` strips
// the role tag (asymmetric specialization at Session.h:646).  Its
// genuine dual is `Select<dual_of(WrongBranchA), dual_of(WrongBranchB)>`.
// Here the SERVER side intentionally has the WRONG branch SHAPE — an
// extra Recv leg the client never offered — so the dual relation
// CANNOT be satisfied in either orientation.  Both `is_dual_v<P1, P2>`
// disjuncts evaluate to false, the static_assert fires, and the
// `[Dual_Mismatch]` diagnostic surfaces.
//
// This fixture is the load-bearing complement to the symmetric-fix:
// it proves that making `is_dual_v` order-insensitive does NOT relax
// detection — a non-dual pair stays a non-dual pair in BOTH
// orientations.
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "Dual_Mismatch"  |
//   "ensure_dual"

#include <crucible/sessions/Session.h>

namespace proto = ::crucible::safety::proto;

struct AliceRole {};
struct WrongBranchA {};
struct WrongBranchB {};
struct StrayBranch {};

using ServerProto =
    proto::Offer<proto::Sender<AliceRole>,
                 proto::Recv<WrongBranchA, proto::End>,
                 proto::Recv<WrongBranchB, proto::End>>;

// Client claims a THREE-branch Select where the server only offers
// TWO — neither `dual_of_t<ServerProto> == ClientProto` nor
// `ServerProto == dual_of_t<ClientProto>` can hold.
using ClientProto =
    proto::Select<proto::Send<WrongBranchA, proto::End>,
                  proto::Send<WrongBranchB, proto::End>,
                  proto::Send<StrayBranch,  proto::End>>;

void compile_time_reject() {
    // Reverse orientation: Server side first, Client side second.
    proto::ensure_dual<ServerProto, ClientProto>();
}

int main() { return 0; }
