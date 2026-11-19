// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The member of the delegation door is public, and it states the whole
// gate of mint_delegated_session.  A handle inside a Loop does not state
// the rest of its protocol, so a direct call of the door with it is
// refused by the same gate as the mint.
//
// Expected diagnostic: the door does not accept the handle.
#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>

#include <utility>

namespace neg_sess_delegation_door_states_the_gate_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_delegation_door_states_the_gate_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_delegation_door_states_the_gate_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<s::Loop<s::Send<int, s::Continue>>>(ctx, Wire{});
    auto carried = s::DelegationDoor::give(std::move(handle));
    static_cast<void>(carried);
    return 0;
}
