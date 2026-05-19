// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-09 negative fixture #3/8:
// `fixy::substr::chainedge::mint_chainedge_waiter<Edge>(edge,
// perm)` rejects when Edge is NOT a ChainEdgeSessionSurface.
//
// Mirrors fixture #1 (signaler_non_surface) on the waiter side:
// proves the ChainEdgeSessionSurface concept gate is preserved
// through the using-decl in Substr.h INDEPENDENTLY of the
// signaler-side instantiation.
//
// Distinct from fixture #4 (waiter_wrong_perm): #3 exercises the
// concept gate on the first (Edge) parameter; #4 exercises the
// second (perm) parameter binding AFTER the concept gate
// succeeds.
//
// Expected diagnostic: "ChainEdgeSessionSurface" / "constraints
// not satisfied" / "no matching function" /
// "mint_chainedge_waiter".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fchain = ::crucible::fixy::substr::chainedge;
namespace saf    = ::crucible::safety;

struct waiter_tag_placeholder {};

int main() {
    int not_an_edge = 0;
    auto perm = saf::mint_permission_root<waiter_tag_placeholder>();

    auto bad = fchain::mint_chainedge_waiter(not_an_edge, std::move(perm));
    (void)bad;
    return 0;
}
