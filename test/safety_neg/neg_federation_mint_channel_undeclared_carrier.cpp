// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// federation::mint_channel accepts a sender endpoint and a receiver
// endpoint only if each one has the session_network value PerPairFifo.
// A carrier without a session_network value does not show that a
// projection is safe on it, and the gate rejects it.
//
// Error: the receiver endpoint has no session_network member.  The sender
// endpoint has the value PerPairFifo.  The context row has IO and Block,
// and the admittance is real.  Only the network gate on the receiver
// endpoint rejects the call.
//
// Expected diagnostic: GCC shows that the fold of CarrierIsPerPairFifo over
// the two endpoints is false.  The sender endpoint is PerPairFifo.  The
// gate rejects the call because of the receiver endpoint.

#include <crucible/permissions/_FederationPermission.h>
#include <crucible/sessions/_FederationProtocol.h>

#include <utility>

namespace fp = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf = ::crucible::safety;
namespace eff = ::crucible::effects;

namespace neg_fed_channel_undeclared {
struct PeerOrg {};
struct TraceKey {};
struct FifoEndpoint {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::PerPairFifo;
};
struct UndeclaredEndpoint {};
}  // namespace neg_fed_channel_undeclared

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake = perm::make_self_signed_handshake<neg_fed_channel_undeclared::PeerOrg>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xFEDCDEULL},
        /*nonce=*/perm::Nonce{0xC0FFEEULL});
    auto admitted = perm::mint_federation_admittance<neg_fed_channel_undeclared::PeerOrg,
                                                     perm::policy::admit_orgs<neg_fed_channel_undeclared::PeerOrg>>(
        local, handshake);
    auto pool = fp::mint_federation_pool<neg_fed_channel_undeclared::PeerOrg>(std::move(*admitted));
    auto guard = pool.lend();

    auto ctx = eff::BgCompileCtx{::crucible::effects::testing::bg()}
                   .in_row<eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>();
    auto channel = fp::mint_channel<neg_fed_channel_undeclared::PeerOrg, neg_fed_channel_undeclared::TraceKey>(
        ctx, neg_fed_channel_undeclared::FifoEndpoint{}, neg_fed_channel_undeclared::UndeclaredEndpoint{},
        guard->token());
    (void)channel;
    return 0;
}

#pragma GCC diagnostic pop
