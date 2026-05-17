// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-13: federation::mint_sender now requires
// `CtxFitsFederation<Ctx>` (Row<IO, Block> ⊆ Ctx::row_type).
//
// Violation (distinct from neg_federation_mint_no_row_fg.cpp):
// BgCompileCtx ships `row_type = Row<Bg, Alloc, IO>` — IO is
// present, but the federation row demands BOTH IO and Block.
// The Block atom is missing.  This rejection class targets the
// "row is a strict subset of what we need" failure mode,
// orthogonal to the "Fg cap cannot widen at all" mode.
//
// Exercises the substrate mint_sender entry point (HS14 floor for
// federation::mint_sender, paired with the channel + sender +
// coord rejections at the wrapper boundary).
//
// Expected diagnostic: CtxFitsFederation / EffectRowMismatch /
// constraints not satisfied.

#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

namespace fp   = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf  = ::crucible::safety;
namespace eff  = ::crucible::effects;

namespace neg_fed_row_bg {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fed_row_bg::PeerOrg>(
            /*peer_key_fp=*/0xFEDCDEULL,
            /*nonce=*/0xC0FFEEULL);
    auto admittance = perm::mint_federation_admittance<
        neg_fed_row_bg::PeerOrg,
        perm::policy::admit_orgs<neg_fed_row_bg::PeerOrg>>(
            local, handshake);

    // BgCompileCtx carries Row<Bg, Alloc, IO> — missing Block.
    eff::BgCompileCtx ctx{};
    auto sender = fp::mint_sender<
        neg_fed_row_bg::PeerOrg, neg_fed_row_bg::TraceKey>(
        ctx, neg_fed_row_bg::Endpoint{}, *admittance);
    (void)sender;
    return 0;
}

#pragma GCC diagnostic pop
