// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit that includes fixy/session/Handle.h defines a class
// with the name of a door that another header could define, and opens a
// session with a permission set whose tokens no mint consumed.  The
// builder takes a SessionOpenKey, and the key befriends only the door of
// the mints, which fixy/session/Handle.h defines.  So the class cannot
// make the key.
//
// Expected diagnostic: the constructor of the key is private in this
// context.
#include <fixy/session/Handle.h>
#include <foundation/Quarantine.h>

#include <source_location>

namespace neg_sess_session_open_key_door_defined_elsewhere_types {
struct Wire {
    int words = 0;
};
struct Region {};
}  // namespace neg_sess_session_open_key_door_defined_elsewhere_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session {
class AsyncChannelDoor {
public:
    static auto open(neg_sess_session_open_key_door_defined_elsewhere_types::Wire wire) {
        return HandleFactory::open_<
            Recv<int, End>, neg_sess_session_open_key_door_defined_elsewhere_types::Wire, check::Enforced,
            ::foundation::permissions::PermSet<neg_sess_session_open_key_door_defined_elsewhere_types::Region>>(
            SessionOpenKey{}, wire, std::source_location::current());
    }
};
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
