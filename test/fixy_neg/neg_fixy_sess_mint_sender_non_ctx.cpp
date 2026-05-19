// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #1/6:
// `fixy::sess::mint_sender<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects a non-IsExecCtx first argument.
//
// mint_sender is re-exported in fixy::sess via `using
// federation::mint_sender;` (Sess.h:285).  The substrate signature
// constrains `Ctx` via the requires-clause
// `CtxFitsFederation<Ctx>`, which itself transitively requires
// `IsExecCtx<Ctx>`.  Passing a plain `int` as the ctx slot fires
// the constraint-satisfaction failure.
//
// Distinct from fixture #2 (no_row_fg, the sibling): #1 exercises
// the IsExecCtx / arity-shape gate by passing a non-class type as
// ctx — substitution fails outright.  #2 exercises the row-subset
// gate by passing a real IsExecCtx (HotFgCtx) with an INSUFFICIENT
// row.  Different failure mechanisms, different diagnostic shapes.
//
// Distinct from the federation_channel fixtures (HS14-05): those
// exercise the 4-arg mint_federation_channel forwarder (combined
// sender+receiver) at the fixy::sess outer boundary.  HS14-06
// exercises the per-role 3-arg mints individually.
//
// Expected diagnostic: "CtxFitsFederation" / "IsExecCtx" /
// "constraints not satisfied".

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp    = ::crucible::safety::proto::federation;
namespace fsess = ::crucible::fixy::sess;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;

namespace neg_fixy_sender_non_ctx {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fixy_sender_non_ctx::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0x5E1DEEULL},
            /*nonce=*/perm::Nonce{0xC0DEABULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fixy_sender_non_ctx::PeerOrg,
        perm::policy::admit_orgs<neg_fixy_sender_non_ctx::PeerOrg>>(
            local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_sender_non_ctx::PeerOrg>(
        std::move(*admitted));
    auto guard = pool.lend();

    int not_a_ctx = 0;
    // Plain int as ctx — fails CtxFitsFederation / IsExecCtx
    // constraint at template parameter substitution time.
    auto bad = fsess::mint_sender<
        neg_fixy_sender_non_ctx::PeerOrg,
        neg_fixy_sender_non_ctx::TraceKey>(
        not_a_ctx,
        neg_fixy_sender_non_ctx::Endpoint{},
        guard->token());
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
