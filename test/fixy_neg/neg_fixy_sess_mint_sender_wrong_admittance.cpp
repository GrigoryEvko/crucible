// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-06 negative fixture #7/9 (super-set extension above
// the ≥2 floor):
// `fixy::sess::mint_sender<Org, KeyTag>(ctx, endpoint, admittance)`
// rejects when the third (admittance) parameter cannot bind to
// `SharedPermission<FederatedPeer<Org>>`.
//
// THIRD distinct mismatch class beyond the floor pair:
//   #1 non_ctx       — fails IsExecCtx prerequisite (ctx slot)
//   #2 no_row_fg     — fails row-subset (ctx slot, post-IsExecCtx)
//   #7 wrong_admit   — fails admittance template-class binding
//                      (third slot — passing `int` instead of
//                      `SharedPermission<FederatedPeer<Org>>`)
//
// Why this is a DISTINCT mismatch class from #1 and #2:
//   - #1/#2 exercise the ctx parameter (first slot) via different
//     constraint-clause failure modes.
//   - #7 exercises the THIRD parameter slot.  IsExecCtx<Ctx>
//     succeeds AND row-subset succeeds (HotBgCtx ships the
//     federation_required_row); the failure is the
//     SharedPermission<FederatedPeer<Org>> template-class
//     binding — `int` cannot bind to that class template.
//
// The §XXI discipline says each mint factory's requires-clause
// AND parameter shape must be witnessed independently — this
// fixture proves the using-decl preserves the parameter-type
// vector beyond just the requires-clause.
//
// Expected diagnostic: "no matching function for call to
// 'mint_sender'" / "cannot convert" / "SharedPermission" /
// "FederatedPeer".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Sess.h>
#include <crucible/sessions/FederationProtocol.h>

namespace fsess = ::crucible::fixy::sess;
namespace eff   = ::crucible::effects;

namespace neg_fixy_sender_wrong_admit {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

int main() {
    // TestRunnerCtx ships row Row<Test, Alloc, IO, Block> —
    // satisfies the federation_required_row subset check
    // (Row<IO, Block>).  IsExecCtx is satisfied.  Row subset is
    // satisfied.  The failure is the third (admittance)
    // parameter type.
    eff::TestRunnerCtx ctx{};
    int not_an_admittance = 0;

    auto bad = fsess::mint_sender<
        neg_fixy_sender_wrong_admit::PeerOrg,
        neg_fixy_sender_wrong_admit::TraceKey>(
        ctx,
        neg_fixy_sender_wrong_admit::Endpoint{},
        not_an_admittance);
    (void)bad;
    return 0;
}
