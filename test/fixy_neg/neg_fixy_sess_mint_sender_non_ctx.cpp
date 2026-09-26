// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// federation::mint_sender<Org, KeyTag>(ctx, endpoint, admittance) rejects
// a first argument that is not an execution context.  Its requires-clause
// CtxFitsFederation<Ctx> contains IsExecCtx<Ctx>, which is false for a
// plain int.
//
// Expected diagnostic: "CtxFitsFederation" / "IsExecCtx" /
// "constraints not satisfied".

#include <crucible/permissions/_FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf = ::crucible::safety;

namespace neg_fixy_sender_non_ctx {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::PerPairFifo;
};
}  // namespace neg_fixy_sender_non_ctx

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake = perm::make_self_signed_handshake<neg_fixy_sender_non_ctx::PeerOrg>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0x5E1DEEULL},
        /*nonce=*/perm::Nonce{0xC0DEABULL});
    auto admitted =
        perm::mint_federation_admittance<neg_fixy_sender_non_ctx::PeerOrg,
                                         perm::policy::admit_orgs<neg_fixy_sender_non_ctx::PeerOrg>>(local, handshake);
    auto pool = fp::mint_federation_pool<neg_fixy_sender_non_ctx::PeerOrg>(std::move(*admitted));
    auto guard = pool.lend();

    int not_a_ctx = 0;
    auto bad = fp::mint_sender<neg_fixy_sender_non_ctx::PeerOrg, neg_fixy_sender_non_ctx::TraceKey>(
        not_a_ctx, neg_fixy_sender_non_ctx::Endpoint{}, guard->token());
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
