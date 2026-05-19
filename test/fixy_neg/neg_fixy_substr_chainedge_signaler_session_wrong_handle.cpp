// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-09 negative fixture #6/8:
// `fixy::substr::chainedge::mint_chainedge_signaler_session<
//      Edge, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to `typename Edge::SignalerHandle&`.
//
// `Edge` is supplied explicitly and IS a known
// ChainEdgeSessionSurface.  `HotFgCtx` IS IsExecCtx.  Both
// concept gates pass; the second parameter `int` cannot bind to
// `Edge::SignalerHandle&` (a class reference).
//
// Distinct from fixture #5 (signaler_session_non_ctx): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the SignalerHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_chainedge_signaler_session'" / "cannot convert" /
// "SignalerHandle" / "mint_chainedge_signaler_session".

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fchain = ::crucible::fixy::substr::chainedge;
namespace conc   = ::crucible::concurrent;
namespace eff    = ::crucible::effects;

namespace neg_fixy_chainedge_signaler_session_wrong_handle {
struct UserTag {};
using Edge = conc::PermissionedChainEdge<conc::VendorBackend::CPU, UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fchain::mint_chainedge_signaler_session<
        neg_fixy_chainedge_signaler_session_wrong_handle::Edge>(
            ctx, not_a_handle);
    (void)bad;
    return 0;
}
