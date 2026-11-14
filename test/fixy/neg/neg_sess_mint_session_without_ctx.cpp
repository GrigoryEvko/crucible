// Each session mint takes the execution context of the caller, and its
// gate admits the effect row of the protocol in that context.  No mint
// opens a session with no context.  This call gives no context, so the
// mint is not viable, and no effect of the protocol escapes the gate.

#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace without_ctx_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using SendsIo = s::Send<IoWork, s::End>;
}  // namespace without_ctx_fixture

int main() {
    auto head = ::fixy::session::mint_session<without_ctx_fixture::SendsIo>(without_ctx_fixture::Channel{});
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
