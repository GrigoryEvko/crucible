// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// federation::mint_coord<Org, KeyTag>(ctx, endpoint, admittance) rejects
// an execution context whose row does not contain IO and Block.  HotFgCtx
// has the empty row, so IsExecCtx holds and the row-subset check inside
// CtxFitsFederation is false.
//
// Expected diagnostic: "CtxFitsFederation" / "EffectRowMismatch" /
// "constraints not satisfied" / "row_subset" / "federation_required_row" /
// "mint_coord".

#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf = ::crucible::safety;
namespace eff = ::crucible::effects;

namespace neg_fixy_coord_no_row {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::PerPairFifo;
};
}  // namespace neg_fixy_coord_no_row

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake = perm::make_self_signed_handshake<neg_fixy_coord_no_row::PeerOrg>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xC04DF1ULL},
        /*nonce=*/perm::Nonce{0xC0DEC4ULL});
    auto admitted =
        perm::mint_federation_admittance<neg_fixy_coord_no_row::PeerOrg,
                                         perm::policy::admit_orgs<neg_fixy_coord_no_row::PeerOrg>>(local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_coord_no_row::PeerOrg>(std::move(*admitted));
    auto guard = pool.lend();

    eff::HotFgCtx fg{};
    auto coord = fp::mint_coord<neg_fixy_coord_no_row::PeerOrg, neg_fixy_coord_no_row::TraceKey>(
        fg, neg_fixy_coord_no_row::Endpoint{}, guard->token());
    (void)coord;
    return 0;
}

#pragma GCC diagnostic pop
