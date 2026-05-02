// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Session integration B (#860): mint_session<Proto>(ctx, res) requires
// the first parameter to satisfy IsExecCtx.  Passing a non-Ctx type
// (a bare int, a non-IsExecCtx struct) is rejected by the
// CtxFitsProtocol concept's IsExecCtx<Ctx> conjunct.
//
// Violation: passes `int{}` as the Ctx parameter; int is not an
// ExecCtx specialization.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at IsExecCtx (inside CtxFitsProtocol).

#include <crucible/sessions/SessionMint.h>

namespace proto = crucible::safety::proto;

struct DummyResource {
    DummyResource() = default;
};

int main() {
    using OkProto = proto::Send<int, proto::End>;
    DummyResource res;
    auto bad = proto::mint_session<OkProto>(int{42}, res);  // IsExecCtx fails
    (void)bad;
    return 0;
}
