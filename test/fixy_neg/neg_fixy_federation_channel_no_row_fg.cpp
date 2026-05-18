// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-13: fixy::sess::mint_federation_channel restates the
// substrate's `CtxFitsFederation<Ctx>` requires-clause at the
// outer boundary so diagnostics fire on the fixy call site, not
// three levels deep in proto::federation::mint_channel.
//
// Violation: HotFgCtx ships `row_type = Row<>` (Fg cap has empty
// permitted row).  Federation requires Row<IO, Block> in scope —
// neither atom can be present.
//
// Pairs with neg_fixy_federation_channel_no_row_bg_compile.cpp
// (distinct rejection class: row contains IO but not Block).
//
// Expected diagnostic: CtxFitsFederation / EffectRowMismatch /
// constraints not satisfied.

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = ::crucible::safety::proto::federation;

namespace fsess = ::crucible::fixy::sess;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;
namespace eff   = ::crucible::effects;

namespace neg_fixy_fed_fg {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fixy_fed_fg::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xFEDCDEULL},
            /*nonce=*/perm::Nonce{0xC0FFEEULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fixy_fed_fg::PeerOrg,
        perm::policy::admit_orgs<neg_fixy_fed_fg::PeerOrg>>(
            local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_fed_fg::PeerOrg>(
        std::move(*admitted));
    auto guard = pool.lend();

    eff::HotFgCtx fg{};
    auto channel = fsess::mint_federation_channel<
        neg_fixy_fed_fg::PeerOrg, neg_fixy_fed_fg::TraceKey>(
        fg,
        neg_fixy_fed_fg::Endpoint{},
        neg_fixy_fed_fg::Endpoint{},
        guard->token());
    (void)channel;
    return 0;
}

#pragma GCC diagnostic pop
