// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-09 negative fixture #4/8:
// `fixy::substr::chainedge::mint_chainedge_waiter<Edge>(edge,
// perm)` rejects when the second (perm) parameter cannot bind to
// `Permission<typename Edge::waiter_tag>&&`.
//
// Mirrors fixture #2 (signaler_wrong_perm) on the waiter side:
// proves the per-mint parameter shape is preserved through the
// using-decl INDEPENDENTLY of the signaler-side instantiation.
//
// `PermissionedChainEdge<VendorBackend::CPU, UserTag>` is a
// known ChainEdgeSessionSurface.  The first parameter binds; the
// concept passes; the second parameter `int` cannot bind to
// `Permission<waiter_tag>&&`.
//
// Expected diagnostic: "no matching function for call to
// 'mint_chainedge_waiter'" / "cannot convert" / "Permission" /
// "mint_chainedge_waiter".

#include <crucible/concurrent/ChainEdge.h>
#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fchain = ::crucible::fixy::substr::chainedge;
namespace conc   = ::crucible::concurrent;

namespace neg_fixy_chainedge_waiter_wrong_perm {
struct UserTag {};
using Edge = conc::PermissionedChainEdge<conc::VendorBackend::CPU, UserTag>;
}

int main() {
    neg_fixy_chainedge_waiter_wrong_perm::Edge edge{
        conc::PlanId{1}, conc::PlanId{2}, conc::ChainEdgeId{0}};
    int not_a_perm = 0;

    auto bad = fchain::mint_chainedge_waiter(edge, not_a_perm);
    (void)bad;
    return 0;
}
