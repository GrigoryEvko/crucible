// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-09 negative fixture #7/8:
// `fixy::substr::chainedge::mint_chainedge_waiter_session<
//      Edge, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// Mirrors fixture #5 (signaler_session_non_ctx) on the waiter
// side: proves the IsExecCtx prerequisite is preserved through
// the using-decl INDEPENDENTLY of the signaler-side
// instantiation.
//
// Distinct from fixture #8 (waiter_session_wrong_handle): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the WaiterHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_chainedge_waiter_session".

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/fixy/Substr.h>

namespace fchain = ::crucible::fixy::substr::chainedge;
namespace conc   = ::crucible::concurrent;

namespace neg_fixy_chainedge_waiter_session_non_ctx {
struct UserTag {};
using Edge = conc::PermissionedChainEdge<conc::VendorBackend::CPU, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_chainedge_waiter_session_non_ctx::Edge::WaiterHandle* handle =
        nullptr;

    auto bad = fchain::mint_chainedge_waiter_session<
        neg_fixy_chainedge_waiter_session_non_ctx::Edge>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
