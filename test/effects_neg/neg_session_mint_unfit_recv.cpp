// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Session integration B (#860): mint_session<Proto>(ctx, res) rejects
// a Recv whose payload's effect row exceeds the Ctx's row.
//
// Violation: HotFgCtx (Row<>) cannot receive a payload that conveys
// Effect::Alloc.  Recv<Capability<Alloc, Bg>, End>'s payload_row =
// Row<Alloc>; not a Subrow of Row<>.  CtxFitsProtocol fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsProtocol.

#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/Capability.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct DummyResource {
    DummyResource() = default;
};

int main() {
    // Receiving an Alloc-conveying capability should be rejected on Fg.
    using AllocCap   = eff::Capability<eff::Effect::Alloc, eff::Bg>;
    using RecvAlloc  = proto::Recv<AllocCap, proto::End>;

    eff::HotFgCtx fg;
    DummyResource res;
    auto bad = proto::mint_session<RecvAlloc>(fg, res);  // CtxFitsProtocol fails
    (void)bad;
    return 0;
}
