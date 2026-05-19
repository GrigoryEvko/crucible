// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #8/9 (super-set extension above
// the ≥2 floor):
// `fixy::sess::mint_receiver<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects when the third (admittance) parameter cannot bind to
// `SharedPermission<FederatedPeer<Org>>`.
//
// THIRD distinct mismatch class beyond the floor pair (#3, #4).
// IsExecCtx satisfied (TestRunnerCtx), row-subset satisfied
// (TestRunnerCtx's row contains IO+Block), failure is on the
// admittance parameter type-binding.
//
// Mirrors mint_sender_wrong_admittance (#7); witnesses per-mint
// signature preservation through the using-decl independently.
//
// Expected diagnostic: "no matching function for call to
// 'mint_receiver'" / "cannot convert" / "SharedPermission" /
// "FederatedPeer".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Sess.h>
#include <crucible/sessions/FederationProtocol.h>

namespace fsess = ::crucible::fixy::sess;
namespace eff   = ::crucible::effects;

namespace neg_fixy_receiver_wrong_admit {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

int main() {
    eff::TestRunnerCtx ctx{};
    int not_an_admittance = 0;

    auto bad = fsess::mint_receiver<
        neg_fixy_receiver_wrong_admit::PeerOrg,
        neg_fixy_receiver_wrong_admit::TraceKey>(
        ctx,
        neg_fixy_receiver_wrong_admit::Endpoint{},
        not_an_admittance);
    (void)bad;
    return 0;
}
