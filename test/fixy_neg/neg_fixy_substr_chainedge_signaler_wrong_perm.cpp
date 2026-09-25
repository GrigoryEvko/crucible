// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chain-edge session mint negative fixture 2 of 8:
// `safety::proto::chainedge_session::mint_chainedge_signaler<Edge>(
// edge, perm)` rejects when the second (perm) parameter cannot bind
// to `Permission<typename Edge::signaler_tag>&&`.
//
// Distinct from fixture #1 (signaler_non_surface): #1 exercises
// the ChainEdgeSessionSurface concept gate on the first (Edge)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// `PermissionedChainEdge<VendorBackend::CPU, UserTag>` is a
// known ChainEdgeSessionSurface.  The first parameter binds; the
// concept passes; the second parameter `int` cannot bind to
// `Permission<signaler_tag>&&`.
//
// Expected diagnostic: "no matching function for call to
// 'mint_chainedge_signaler'" / "cannot convert" / "Permission" /
// "mint_chainedge_signaler".

#include <crucible/concurrent/_ChainEdge.h>
#include <crucible/concurrent/_PermissionedChainEdge.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_ChainEdgeSession.h>

namespace fchain = ::crucible::safety::proto::chainedge_session;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_chainedge_signaler_wrong_perm {
struct UserTag {};
using Edge = conc::PermissionedChainEdge<conc::VendorBackend::CPU, UserTag>;
}  // namespace neg_fixy_chainedge_signaler_wrong_perm

int main() {
    neg_fixy_chainedge_signaler_wrong_perm::Edge edge{conc::PlanId{1}, conc::PlanId{2}, conc::ChainEdgeId{0}};
    int not_a_perm = 0;

    auto bad = fchain::mint_chainedge_signaler(edge, not_a_perm);
    (void)bad;
    return 0;
}
