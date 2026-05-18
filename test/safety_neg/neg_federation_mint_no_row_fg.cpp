// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-13: federation::mint_channel now requires
// `CtxFitsFederation<Ctx>` (Row<IO, Block> ⊆ Ctx::row_type).
//
// Violation: HotFgCtx ships `cap_type = ctx_cap::Fg`, whose
// `cap_permitted_row = Row<>`.  No widening can introduce IO or
// Block atoms into a Fg-rooted ctx — `.in_row<>()` rejects rows
// not contained in the cap's permitted row.  The substrate mint
// rejects HotFgCtx at the requires-clause before any session
// machinery executes.
//
// Expected diagnostic: CtxFitsFederation / EffectRowMismatch /
// constraints not satisfied.
//
// Pairs with neg_federation_mint_no_row_bg_drain.cpp (distinct
// rejection class: BgCompileCtx missing only the Block atom).

#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp   = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf  = ::crucible::safety;
namespace eff  = ::crucible::effects;

namespace neg_fed_row_fg {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    // Mint a legitimate admittance so the call site is otherwise
    // well-formed; only the ctx row gate is what fails.  Per
    // fixy-A2-009 federation mints now take a SharedPermission witness;
    // park the exclusive Permission in a pool and lend a token.
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fed_row_fg::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xFEDCDEULL},
            /*nonce=*/perm::Nonce{0xC0FFEEULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fed_row_fg::PeerOrg,
        perm::policy::admit_orgs<neg_fed_row_fg::PeerOrg>>(
            local, handshake);
    auto pool = fp::mint_federation_pool<neg_fed_row_fg::PeerOrg>(
        std::move(*admitted));
    auto guard = pool.lend();

    eff::HotFgCtx fg{};
    auto channel = fp::mint_channel<
        neg_fed_row_fg::PeerOrg, neg_fed_row_fg::TraceKey>(
        fg,
        neg_fed_row_fg::Endpoint{},
        neg_fed_row_fg::Endpoint{},
        guard->token());
    (void)channel;
    return 0;
}

#pragma GCC diagnostic pop
