// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Session integration B (#860): mint_session<Proto>(ctx, res) rejects
// a foreground Ctx paired with a protocol whose Send payload carries
// a Bg effect row.
//
// Violation: HotFgCtx has row Row<>; the protocol's Send payload is
// Computation<Row<Bg>, int> which carries Row<Bg>.  Subrow<Row<Bg>,
// Row<>> = false.  CtxFitsProtocol fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsProtocol.

#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/Computation.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct DummyResource {
    DummyResource() = default;
};

int main() {
    using BgPayload  = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
    using BgProto    = proto::Send<BgPayload, proto::End>;

    eff::HotFgCtx fg;
    DummyResource res;
    auto bad = proto::mint_session<BgProto>(fg, res);  // CtxFitsProtocol fails
    (void)bad;
    return 0;
}
