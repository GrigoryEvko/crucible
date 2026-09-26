// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// federation::mint_sender accepts only a carrier with the session_network
// value PerPairFifo.  A projection of a global type is safe on one FIFO
// queue for each ordered pair of roles, and not on a mailbox.
//
// Error: the endpoint has the session_network value Mailbox.  The context
// row has IO and Block, and the admittance is real.  Only the network
// gate CarrierIsPerPairFifo rejects the call.
//
// Expected diagnostic: GCC shows that CarrierIsPerPairFifo is false,
// because the session_network value is not PerPairFifo.

#include <crucible/permissions/_FederationPermission.h>
#include <crucible/sessions/_FederationProtocol.h>

#include <utility>

namespace fp = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf = ::crucible::safety;
namespace eff = ::crucible::effects;

namespace neg_fed_sender_mailbox {
struct PeerOrg {};
struct TraceKey {};
struct MailboxEndpoint {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::Mailbox;
};
}  // namespace neg_fed_sender_mailbox

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake = perm::make_self_signed_handshake<neg_fed_sender_mailbox::PeerOrg>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xFEDCDEULL},
        /*nonce=*/perm::Nonce{0xC0FFEEULL});
    auto admitted = perm::mint_federation_admittance<neg_fed_sender_mailbox::PeerOrg,
                                                     perm::policy::admit_orgs<neg_fed_sender_mailbox::PeerOrg>>(
        local, handshake);
    auto pool = fp::mint_federation_pool<neg_fed_sender_mailbox::PeerOrg>(std::move(*admitted));
    auto guard = pool.lend();

    auto ctx = eff::BgCompileCtx{::crucible::effects::testing::bg()}
                   .in_row<eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>();
    auto sender = fp::mint_sender<neg_fed_sender_mailbox::PeerOrg, neg_fed_sender_mailbox::TraceKey>(
        ctx, neg_fed_sender_mailbox::MailboxEndpoint{}, guard->token());
    (void)sender;
    return 0;
}

#pragma GCC diagnostic pop
