// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The members of the recording door are public, and the opening member
// states the whole gate of mint_recorded_session.  A direct call of the
// door with a value that is no session handle, which the mint refuses, is
// refused by the same gate, so the door is no weaker than the mint.
//
// Expected diagnostic: no member of the door accepts the value.
#include <fixy/session/Recording.h>

namespace neg_sess_record_door_states_the_gate_types {
struct NotAHandle {
    int words = 0;
};
}  // namespace neg_sess_record_door_states_the_gate_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_record_door_states_the_gate_types;
    s::SessionEventLog log;
    auto forged = s::RecordingDoor::make<NotAHandle>(NotAHandle{}, log, s::RoleTagId{1}, s::RoleTagId{2});
    static_cast<void>(forged);
    return 0;
}
