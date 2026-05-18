// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-13: fixy::sess::mint_federation_channel restates the
// substrate's `CtxFitsFederation<Ctx>` requires-clause.  This
// fixture exercises the "row is a strict subset" rejection mode
// distinct from the "Fg cap empty row" mode covered by
// neg_fixy_federation_channel_no_row_fg.cpp.
//
// Violation: BgCompileCtx carries Row<Bg, Alloc, IO>.  IO is
// present, but federation also requires Block — Subrow check
// fails on the missing Block atom.
//
// Expected diagnostic: CtxFitsFederation / EffectRowMismatch /
// constraints not satisfied.

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>

namespace fsess = ::crucible::fixy::sess;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;
namespace eff   = ::crucible::effects;

namespace neg_fixy_fed_bg {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fixy_fed_bg::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xFEDCDEULL},
            /*nonce=*/perm::Nonce{0xC0FFEEULL});
    auto admittance = perm::mint_federation_admittance<
        neg_fixy_fed_bg::PeerOrg,
        perm::policy::admit_orgs<neg_fixy_fed_bg::PeerOrg>>(
            local, handshake);

    // BgCompileCtx carries Row<Bg, Alloc, IO> — missing Block.
    eff::BgCompileCtx ctx{};
    auto channel = fsess::mint_federation_channel<
        neg_fixy_fed_bg::PeerOrg, neg_fixy_fed_bg::TraceKey>(
        ctx,
        neg_fixy_fed_bg::Endpoint{},
        neg_fixy_fed_bg::Endpoint{},
        *admittance);
    (void)channel;
    return 0;
}

#pragma GCC diagnostic pop
