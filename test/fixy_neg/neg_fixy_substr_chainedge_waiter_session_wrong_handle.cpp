// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chain-edge session mint negative fixture 8 of 8:
// `safety::proto::chainedge_session::mint_chainedge_waiter_session<
//      Edge, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to `typename Edge::WaiterHandle&`.
//
// Mirrors fixture #6 (signaler_session_wrong_handle) on the
// waiter side: proves the WaiterHandle reference binding fires
// INDEPENDENTLY of the signaler-side instantiation.
//
// Distinct from fixture #7 (waiter_session_non_ctx): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the WaiterHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_chainedge_waiter_session'" / "cannot convert" /
// "WaiterHandle" / "mint_chainedge_waiter_session".

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/ChainEdgeSession.h>

namespace fchain = ::crucible::safety::proto::chainedge_session;
namespace conc = ::crucible::concurrent;
namespace eff = ::crucible::effects;

namespace neg_fixy_chainedge_waiter_session_wrong_handle {
struct UserTag {};
using Edge = conc::PermissionedChainEdge<conc::VendorBackend::CPU, UserTag>;
}  // namespace neg_fixy_chainedge_waiter_session_wrong_handle

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad =
        fchain::mint_chainedge_waiter_session<neg_fixy_chainedge_waiter_session_wrong_handle::Edge>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
