// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #2/6:
// `fixy::sess::mint_sender<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects when ctx is IsExecCtx but its row is INSUFFICIENT.
//
// Violation: HotFgCtx ships `row_type = Row<>` (Fg cap has empty
// permitted row).  Federation requires Row<IO, Block> in scope —
// the row-subset check inside CtxFitsFederation fails.
//
// Distinct from fixture #1 (non_ctx): #1 passes `int` and fails
// the IsExecCtx prerequisite of CtxFitsFederation; #2 passes a
// REAL IsExecCtx (HotFgCtx) and fails the row-subset
// (CRUCIBLE_ROW_MISMATCH_ASSERT in the body or the requires-clause
// row check) AFTER the IsExecCtx prerequisite is satisfied.
// Different mechanisms, different diagnostic shapes.
//
// Expected diagnostic: "CtxFitsFederation" /
// "EffectRowMismatch" / "constraints not satisfied" /
// "row_subset" / "federation_required_row".

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = ::crucible::safety::proto::federation;

namespace fsess = ::crucible::fixy::sess;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;
namespace eff   = ::crucible::effects;

namespace neg_fixy_sender_no_row {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fixy_sender_no_row::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0x5E1FF1ULL},
            /*nonce=*/perm::Nonce{0xC0DE5EULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fixy_sender_no_row::PeerOrg,
        perm::policy::admit_orgs<neg_fixy_sender_no_row::PeerOrg>>(
            local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_sender_no_row::PeerOrg>(
        std::move(*admitted));
    auto guard = pool.lend();

    eff::HotFgCtx fg{};
    auto sender = fsess::mint_sender<
        neg_fixy_sender_no_row::PeerOrg, neg_fixy_sender_no_row::TraceKey>(
        fg,
        neg_fixy_sender_no_row::Endpoint{},
        guard->token());
    (void)sender;
    return 0;
}

#pragma GCC diagnostic pop
