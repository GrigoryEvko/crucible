// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// federation::mint_coord<Org, KeyTag>(ctx, endpoint, admittance) rejects
// a third argument that is not SharedPermission<FederatedPeer<Org>>.  The
// context satisfies IsExecCtx and the row check, so only the type of the
// admittance parameter stops the call.
//
// Expected diagnostic: "no matching function for call to 'mint_coord'" /
// "cannot convert" / "SharedPermission" / "FederatedPeer".

#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_FederationProtocol.h>

namespace fp = ::crucible::safety::proto::federation;
namespace eff = ::crucible::effects;

namespace neg_fixy_coord_wrong_admit {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::PerPairFifo;
};
}  // namespace neg_fixy_coord_wrong_admit

int main() {
    eff::TestRunnerCtx ctx{::crucible::effects::testing::test()};
    int not_an_admittance = 0;

    auto bad = fp::mint_coord<neg_fixy_coord_wrong_admit::PeerOrg, neg_fixy_coord_wrong_admit::TraceKey>(
        ctx, neg_fixy_coord_wrong_admit::Endpoint{}, not_an_admittance);
    (void)bad;
    return 0;
}
