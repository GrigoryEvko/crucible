// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-09 negative fixture #5/8:
// `fixy::substr::chainedge::mint_chainedge_signaler_session<
//      Edge, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// `Edge` is supplied explicitly (it appears in non-deduced
// position via `typename Edge::SignalerHandle&`) and IS a known
// ChainEdgeSessionSurface.  The concept check on Edge passes;
// `Ctx` is deduced from the first argument as `int`; `IsExecCtx`
// rejects it.
//
// Distinct from fixture #6 (signaler_session_wrong_handle): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the SignalerHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_chainedge_signaler_session".

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/fixy/Substr.h>

namespace fchain = ::crucible::fixy::substr::chainedge;
namespace conc   = ::crucible::concurrent;

namespace neg_fixy_chainedge_signaler_session_non_ctx {
struct UserTag {};
using Edge = conc::PermissionedChainEdge<conc::VendorBackend::CPU, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_chainedge_signaler_session_non_ctx::Edge::SignalerHandle* handle =
        nullptr;

    auto bad = fchain::mint_chainedge_signaler_session<
        neg_fixy_chainedge_signaler_session_non_ctx::Edge>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
