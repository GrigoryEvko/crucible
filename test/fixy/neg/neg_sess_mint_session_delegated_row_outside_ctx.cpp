// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol sends an int and no computation, but it delegates an
// endpoint whose protocol sends a computation that needs IO.  The
// recipient runs that protocol with what this session gave it, so the
// payloads of a delegated protocol count.  The background context holds
// no IO, so mint_session rejects the call.
//
// Expected diagnostic: CtxAdmitsProtocolRow is not satisfied.

#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/PermSet.h>

#include <utility>

namespace delegated_row_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Wire {};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using Inner = s::Send<IoWork, s::End>;
using Handed = s::DelegatedSession<Inner, Wire, s::DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>;
using Proto = s::Send<int, s::Send<Handed, s::End>>;
}  // namespace delegated_row_fixture

int main() {
    using namespace delegated_row_fixture;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
