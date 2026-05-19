// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #5/6:
// `fixy::sess::mint_coord<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects a non-IsExecCtx first argument.
//
// Same gate structure as mint_sender/mint_receiver:
// CtxFitsFederation requires IsExecCtx<Ctx>; passing `int` fails
// substitution.
//
// Distinct from mint_coord no_row_fg (#6): #5 hits the IsExecCtx
// prerequisite; #6 hits the row-subset check.
//
// Distinct from mint_sender/mint_receiver fixtures: different §XXI
// mint factory, different protocol type (CoordProto vs Sender/
// ReceiverProto).  HS14 floor requires per-mint coverage; using-
// decl from `federation::mint_coord` (Sess.h:287) must
// independently preserve the requires-clause.
//
// Expected diagnostic: "CtxFitsFederation" / "IsExecCtx" /
// "constraints not satisfied" / "mint_coord".

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp    = ::crucible::safety::proto::federation;
namespace fsess = ::crucible::fixy::sess;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;

namespace neg_fixy_coord_non_ctx {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fixy_coord_non_ctx::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xC04DCAULL},
            /*nonce=*/perm::Nonce{0xC0DEC3ULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fixy_coord_non_ctx::PeerOrg,
        perm::policy::admit_orgs<neg_fixy_coord_non_ctx::PeerOrg>>(
            local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_coord_non_ctx::PeerOrg>(
        std::move(*admitted));
    auto guard = pool.lend();

    int not_a_ctx = 0;
    auto bad = fsess::mint_coord<
        neg_fixy_coord_non_ctx::PeerOrg,
        neg_fixy_coord_non_ctx::TraceKey>(
        not_a_ctx,
        neg_fixy_coord_non_ctx::Endpoint{},
        guard->token());
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
