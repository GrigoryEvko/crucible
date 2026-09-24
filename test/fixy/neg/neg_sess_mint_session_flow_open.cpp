// mint_session starts the session with the empty permission set.  This
// protocol sends a Transferable, which moves the token of Region to the
// peer, and the empty set holds no such token.  The permission flow does
// not close, so the gate refuses the call.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace flow_open_fixture {
namespace s = ::fixy::session;
struct Region {};
struct Wire {};
using SendsRegion = s::Send<s::Transferable<int, Region>, s::End>;
}  // namespace flow_open_fixture

int main() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto head = ::fixy::session::mint_session<flow_open_fixture::SendsRegion>(ctx, flow_open_fixture::Wire{});
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
