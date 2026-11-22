// Negative compile test.  This file must fail to compile.
//
// A named state stands for the protocol of its name, and the walks of the
// session layer follow that protocol.  Ping and Pong below name each other,
// so the named state of Ping reaches itself.  A cycle of names is a
// recursion, and a recursion is a Loop with its Continue inside the protocol
// of one name.  The mint refuses the cycle before a walk follows it.
//
// Expected diagnostic: Protocol_Named_State_Cycle, which names the state.
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

namespace neg_sess_named_state_cycle_types {
struct Wire {};
struct Pong;
struct Ping {
    using protocol = ::fixy::session::Send<int, ::fixy::session::Named<Pong>>;
};
struct Pong {
    using protocol = ::fixy::session::Recv<int, ::fixy::session::Named<Ping>>;
};
}  // namespace neg_sess_named_state_cycle_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_named_state_cycle_types;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto refused = s::mint_session<s::Named<Ping>>(ctx, Wire{});
    std::move(refused).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
