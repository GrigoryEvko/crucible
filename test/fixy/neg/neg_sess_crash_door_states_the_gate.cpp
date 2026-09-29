// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The members of the crash door are public, and the opening member states
// the whole gate of mint_crash_session.  A direct call of the door with a
// protocol that the mint refuses, a bare reception from an unreliable
// peer, is refused by the same gate, so the door is no weaker than the
// mint.
//
// Expected diagnostic: no member of the door accepts the protocol.
#include <fixy/session/CrashTransport.h>

#include <source_location>

namespace neg_sess_crash_door_states_the_gate_types {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace neg_sess_crash_door_states_the_gate_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_crash_door_states_the_gate_types;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    auto forged =
        s::CrashSessionDoor::open<s::Recv<int, s::End>, Alice, Bob, s::ReliableSet<>, s::DefaultAbandonmentPolicy>(
            ctx, Wire{}, cell, s::mint_crash_writer(own), std::source_location{});
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
