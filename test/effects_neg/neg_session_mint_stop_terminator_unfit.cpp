// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-4 (S2.4): pins the Stop terminator's walker spec.  The
// SessionMint walker handles BSYZ22 crash-stop's Stop the same way
// as End — vacuously admitted, but the surrounding Send's payload
// row still counts.
//
// Violation: Send<Computation<Row<Bg>, int>, Stop> has the same
// Bg-effect requirement as Send<..., End>.  HotFgCtx (Row<>) cannot
// admit Bg.  The walker correctly walks past Stop and fails on the
// Send's payload row.
//
// Without the Stop walker spec (added in session-B), this would fall
// to the primary template's false_type, which would also fail —
// but for the WRONG reason (unrecognized terminator instead of row
// mismatch).  This fixture pins that the walker handles Stop
// correctly AND the row mismatch fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsProtocol.

#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/effects/Computation.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct DummyResource {
    DummyResource() = default;
};

int main() {
    using BgPayload = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
    // Send terminating in Stop, NOT End — exercises the Stop walker spec.
    using BgSendStop = proto::Send<BgPayload, proto::Stop>;

    eff::HotFgCtx fg;
    DummyResource res;
    auto bad = proto::mint_session<BgSendStop>(fg, res);  // CtxFitsProtocol fails on Send's payload row
    (void)bad;
    return 0;
}
