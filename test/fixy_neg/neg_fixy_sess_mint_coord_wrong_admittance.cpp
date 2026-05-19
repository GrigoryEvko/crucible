// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #9/9 (super-set extension above
// the ≥2 floor):
// `fixy::sess::mint_coord<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects when the third (admittance) parameter cannot bind to
// `SharedPermission<FederatedPeer<Org>>`.
//
// THIRD distinct mismatch class beyond the floor pair (#5, #6).
// IsExecCtx satisfied (TestRunnerCtx), row-subset satisfied, the
// failure is on the admittance parameter type-binding.
//
// Mirrors mint_sender_wrong_admittance (#7) and
// mint_receiver_wrong_admittance (#8); witnesses per-mint
// signature preservation through the using-decl independently.
//
// Expected diagnostic: "no matching function for call to
// 'mint_coord'" / "cannot convert" / "SharedPermission" /
// "FederatedPeer".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Sess.h>
#include <crucible/sessions/FederationProtocol.h>

namespace fsess = ::crucible::fixy::sess;
namespace eff   = ::crucible::effects;

namespace neg_fixy_coord_wrong_admit {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

int main() {
    eff::TestRunnerCtx ctx{};
    int not_an_admittance = 0;

    auto bad = fsess::mint_coord<
        neg_fixy_coord_wrong_admit::PeerOrg,
        neg_fixy_coord_wrong_admit::TraceKey>(
        ctx,
        neg_fixy_coord_wrong_admit::Endpoint{},
        not_an_admittance);
    (void)bad;
    return 0;
}
