// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #3/6:
// `fixy::sess::mint_receiver<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects a non-IsExecCtx first argument.
//
// Same gate structure as mint_sender (HS14-06 #1): CtxFitsFederation
// requires IsExecCtx<Ctx>; passing `int` fails substitution.
//
// Distinct from mint_receiver no_row_fg (#4): #3 hits the
// IsExecCtx prerequisite by passing a non-class type; #4 hits the
// row-subset check after IsExecCtx is satisfied.
//
// Distinct from mint_sender fixtures (#1/#2): different §XXI
// mint factory, different protocol type (ReceiverProto vs
// SenderProto).  HS14 floor requires per-mint coverage; using-decl
// from `federation::mint_receiver` (Sess.h:286) must independently
// preserve the requires-clause.
//
// Expected diagnostic: "CtxFitsFederation" / "IsExecCtx" /
// "constraints not satisfied" / "mint_receiver".

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp    = ::crucible::safety::proto::federation;
namespace fsess = ::crucible::fixy::sess;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;

namespace neg_fixy_receiver_non_ctx {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fixy_receiver_non_ctx::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xCEDEAFULL},
            /*nonce=*/perm::Nonce{0xC0DEC1ULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fixy_receiver_non_ctx::PeerOrg,
        perm::policy::admit_orgs<neg_fixy_receiver_non_ctx::PeerOrg>>(
            local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_receiver_non_ctx::PeerOrg>(
        std::move(*admitted));
    auto guard = pool.lend();

    int not_a_ctx = 0;
    auto bad = fsess::mint_receiver<
        neg_fixy_receiver_non_ctx::PeerOrg,
        neg_fixy_receiver_non_ctx::TraceKey>(
        not_a_ctx,
        neg_fixy_receiver_non_ctx::Endpoint{},
        guard->token());
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
