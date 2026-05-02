// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Session integration B (#860): mint_session<CheckpointedSession<B,
// R>>(ctx, res) walks BOTH branches.  An admitted Base + an
// inadmissible Rollback fails the whole check — rollback IS reachable
// on checkpoint failure, so its row counts.
//
// Violation: HotFgCtx admits Send<int, End> (Base) but NOT
// Recv<Computation<Row<Bg>, int>, End> (Rollback).
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsProtocol.

#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/effects/Computation.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct DummyResource {
    DummyResource() = default;
};

int main() {
    using BgPayload = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

    using Base     = proto::Send<int, proto::End>;          // Row<>
    using Rollback = proto::Recv<BgPayload, proto::End>;    // Row<Bg>
    using Ckpt     = proto::CheckpointedSession<Base, Rollback>;

    eff::HotFgCtx fg;
    DummyResource res;
    auto bad = proto::mint_session<Ckpt>(fg, res);          // Rollback unfit on Fg
    (void)bad;
    return 0;
}
