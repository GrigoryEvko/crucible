// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-2 (CLAUDE.md §XXI HS14): proves the payload_row<HotPath<Tier,
// T>> transparent-unwrap specialization correctly sees through the
// HotPath wrapper to the inner Computation<Row<Bg>, T>.  Before this
// audit fix, the missing specialization fell through the primary
// template (yielding Row<>) and silently admitted Bg-effect payloads
// on a HotFgCtx.  This fixture verifies the soundness gap is closed.
//
// Violation: Send<HotPath<Hot, Computation<Row<Bg>, int>>, End>'s
// payload_row is now Row<Bg>, not Row<>.  CtxFitsProtocol fails on
// HotFgCtx (which has Row<>).
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsProtocol.
//
// If this fixture STARTS PASSING (i.e., the file compiles), it means
// the payload_row<HotPath<...>> transparent-unwrap was removed —
// SOUNDNESS REGRESSION.  Investigate immediately.

#include <crucible/effects/Computation.h>
#include <crucible/safety/HotPath.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = crucible::effects;
namespace saf   = crucible::safety;
namespace proto = crucible::safety::proto;

struct DummyResource {
    DummyResource() = default;
};

int main() {
    using BgComp     = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
    using HotBgPayload = saf::HotPath<saf::HotPathTier_v::Hot, BgComp>;
    using BadProto   = proto::Send<HotBgPayload, proto::End>;

    eff::HotFgCtx fg;
    DummyResource res;
    auto bad = proto::mint_session<BadProto>(fg, res);  // CtxFitsProtocol fails — HotPath sees through to Bg
    (void)bad;
    return 0;
}
