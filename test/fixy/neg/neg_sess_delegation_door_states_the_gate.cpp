// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The member of the delegation door is public, and it states the whole
// gate of mint_delegated_session.  A handle inside a Loop does not state
// the rest of its protocol, so a direct call of the door with it is
// refused by the same gate as the mint.
//
// Expected diagnostic: the door does not accept the handle.
#include <fixy/session/Delegate.h>

#include <utility>

namespace neg_sess_delegation_door_states_the_gate_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_delegation_door_states_the_gate_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_delegation_door_states_the_gate_types;
    auto handle = s::mint_session_handle<s::Loop<s::Send<int, s::Continue>>, Wire>(Wire{});
    auto carried = s::DelegationDoor::give(std::move(handle), s::mint_permission_hold());
    static_cast<void>(carried);
    return 0;
}
