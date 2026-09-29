// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The members of the door of the mints are public, and each one states
// the whole gate of its mint.  A direct call of the door with a protocol
// that the mint refuses is refused by the same gate, so the door is no
// weaker than the mint.
//
// Expected diagnostic: no member of the door accepts the protocol.
#include <fixy/session/Handle.h>

#include <source_location>

namespace neg_sess_mint_door_states_the_gate_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_mint_door_states_the_gate_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_mint_door_states_the_gate_types;
    auto head = s::SessionMintDoor::open<s::Send<int, s::Continue>, Wire, s::DefaultAbandonmentPolicy>(
        Wire{}, std::source_location{});
    static_cast<void>(head);
    return 0;
}
