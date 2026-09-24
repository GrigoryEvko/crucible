// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol receives a computation that needs IO.  The receiver then
// holds that computation, so a received payload counts as a sent one
// does.  The background context holds no IO, so mint_session rejects the
// call.
//
// Expected diagnostic: CtxAdmitsProtocolRow is not satisfied.

#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace recv_row_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Wire {};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using Proto = s::Recv<IoWork, s::End>;
}  // namespace recv_row_fixture

int main() {
    using namespace recv_row_fixture;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
