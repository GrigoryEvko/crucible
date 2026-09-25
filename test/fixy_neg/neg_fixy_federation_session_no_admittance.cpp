// fixy_neg: mint_sender does not mint a federation session when the caller
// gives no FederatedPeer admittance.
//
// The mint takes (ctx, endpoint, SharedPermission<FederatedPeer<Org>>), and
// the admittance has no default.  A call with only a context and an
// endpoint matches no overload.  Without the admittance the local side has
// no proof that it was admitted to talk to Org.
//
// Expected diagnostic: "no matching function for call to 'mint_sender(...)'" /
// "requires 3 arguments" / "too few arguments to function".

#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/FederationProtocol.h>

namespace fp = crucible::safety::proto::federation;

struct NegFedNoAdmit_PeerOrg {};
struct NegFedNoAdmit_KeyTag {};
struct NegFedNoAdmit_Endpoint {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::PerPairFifo;
};

int main() {
    crucible::effects::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    NegFedNoAdmit_Endpoint endpoint{};

    auto bad = fp::mint_sender<NegFedNoAdmit_PeerOrg, NegFedNoAdmit_KeyTag>(ctx, endpoint);
    (void)bad;
    return 0;
}
