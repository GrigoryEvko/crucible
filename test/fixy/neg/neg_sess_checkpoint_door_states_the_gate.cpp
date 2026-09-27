// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The members of the checkpoint door are public, and the opening member
// states the whole gate of mint_checkpoint_session.  A direct call of the
// door with a pair in which both sides choose, which the mint refuses, is
// refused by the same gate, so the door is no weaker than the mint.
//
// Expected diagnostic: no member of the door accepts the pair.
#include <fixy/session/Checkpoint.h>

#include <source_location>

namespace neg_sess_checkpoint_door_states_the_gate_types {
struct Wire {};
}  // namespace neg_sess_checkpoint_door_states_the_gate_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_door_states_the_gate_types;
    using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto forged =
        s::CheckpointDoor::open<Decide, Decide, s::DefaultAbandonmentPolicy>(ctx, Wire{}, std::source_location{});
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
