// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol sends a computation that needs IO.  The background
// context holds Bg and Alloc, and no IO.  The context must hold each
// effect that a payload of the session carries, so mint_session rejects
// the call.
//
// Expected diagnostic: CtxAdmitsProtocolRow is not satisfied.

#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace send_row_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Wire {};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using Proto = s::Send<IoWork, s::End>;
}  // namespace send_row_fixture

int main() {
    using namespace send_row_fixture;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
