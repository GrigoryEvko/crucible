// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol of the mint receives an endpoint, and the receiver runs the
// protocol of that endpoint.  So the walk of the regions that a protocol
// delivers (fixy/session/Flow.h) reads that protocol too.  Ping and Pong
// below name each other, so the named state of Ping reaches itself in the
// protocol of the received endpoint.  A cycle of names is a recursion, and a
// recursion is a Loop with its Continue inside the protocol of one name.  The
// walk refuses the cycle before it folds that protocol.
//
// Expected diagnostic: Protocol_Named_State_Cycle, which names the state.
#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

namespace neg_sess_received_endpoint_with_a_cycle_of_names_types {
struct Counter {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::Local;
    [[no_unique_address]] ::fixy::session::MoveOnlyResource move_only{};
};
struct Pong;
struct Ping {
    using protocol = ::fixy::session::Send<int, ::fixy::session::Named<Pong>>;
};
struct Pong {
    using protocol = ::fixy::session::Recv<int, ::fixy::session::Named<Ping>>;
};
using Receiver =
    ::fixy::session::Recv<::fixy::session::DelegatedSession<::fixy::session::Send<int, ::fixy::session::Named<Ping>>,
                                                            Counter, ::fixy::session::DefaultAbandonmentPolicy,
                                                            ::foundation::permissions::EmptyPermSet>,
                          ::fixy::session::End>;
}  // namespace neg_sess_received_endpoint_with_a_cycle_of_names_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_received_endpoint_with_a_cycle_of_names_types;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto refused = s::mint_session<Receiver>(ctx, Counter{});
    std::move(refused).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
