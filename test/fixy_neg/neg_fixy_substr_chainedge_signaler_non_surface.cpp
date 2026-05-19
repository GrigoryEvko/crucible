// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-09 negative fixture #1/8:
// `fixy::substr::chainedge::mint_chainedge_signaler<Edge>(edge,
// perm)` rejects when Edge is NOT a ChainEdgeSessionSurface.
//
// `int` lacks the ChainEdgeSessionSurface concept's required
// nested types (signaler_tag, waiter_tag, SignalerHandle,
// WaiterHandle, value_type) and the signaler/waiter factory
// members.  The requires-clause fires at substitution time.
//
// Distinct from fixture #2 (wrong_perm): #1 exercises the
// ChainEdgeSessionSurface concept gate on the first (Edge)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// Expected diagnostic: "ChainEdgeSessionSurface" / "constraints
// not satisfied" / "no matching function" /
// "mint_chainedge_signaler".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fchain = ::crucible::fixy::substr::chainedge;
namespace saf    = ::crucible::safety;

struct signaler_tag_placeholder {};

int main() {
    int not_an_edge = 0;
    auto perm = saf::mint_permission_root<signaler_tag_placeholder>();

    auto bad = fchain::mint_chainedge_signaler(not_an_edge, std::move(perm));
    (void)bad;
    return 0;
}
