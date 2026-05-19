// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #4/6:
// `fixy::sess::mint_receiver<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects when ctx is IsExecCtx but its row is INSUFFICIENT.
//
// HotFgCtx ships `row_type = Row<>`; federation requires
// Row<IO, Block>.  CtxFitsFederation's row-subset check fails.
//
// Distinct from fixture #3 (non_ctx): #3 passes `int` (fails
// IsExecCtx prerequisite); #4 passes real IsExecCtx with wrong row
// (fails row-subset).
//
// Distinct from mint_sender no_row_fg (#2): same gate mechanism
// but different §XXI mint factory.  HS14 floor requires each mint
// to independently witness its requires-clause discipline.
//
// Expected diagnostic: "CtxFitsFederation" /
// "EffectRowMismatch" / "constraints not satisfied" /
// "row_subset" / "federation_required_row" / "mint_receiver".

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = ::crucible::safety::proto::federation;

namespace fsess = ::crucible::fixy::sess;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;
namespace eff   = ::crucible::effects;

namespace neg_fixy_receiver_no_row {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fixy_receiver_no_row::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xCEDFEDULL},
            /*nonce=*/perm::Nonce{0xC0DEC2ULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fixy_receiver_no_row::PeerOrg,
        perm::policy::admit_orgs<neg_fixy_receiver_no_row::PeerOrg>>(
            local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_receiver_no_row::PeerOrg>(
        std::move(*admitted));
    auto guard = pool.lend();

    eff::HotFgCtx fg{};
    auto receiver = fsess::mint_receiver<
        neg_fixy_receiver_no_row::PeerOrg,
        neg_fixy_receiver_no_row::TraceKey>(
        fg,
        neg_fixy_receiver_no_row::Endpoint{},
        guard->token());
    (void)receiver;
    return 0;
}

#pragma GCC diagnostic pop
