// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit that includes fixy/session/Handle.h and not
// fixy/session/Checkpoint.h defines a class with the name of the
// checkpoint door, and calls a builder of the handle factory from it.
// The factory befriends no class that another header defines, so the
// class gets no access, and the builder stays private.  A builder in
// reach would give a handle at a protocol that is not even well-formed.
//
// Expected diagnostic: the builder is private in this context.
#include <fixy/session/Handle.h>

namespace neg_sess_handle_factory_door_defined_elsewhere_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_handle_factory_door_defined_elsewhere_types

namespace fixy::session {
class CheckpointDoor {
public:
    static auto forge(neg_sess_handle_factory_door_defined_elsewhere_types::Wire wire) {
        return HandleFactory::make_<Recv<int, Continue>, neg_sess_handle_factory_door_defined_elsewhere_types::Wire, void, check::Enforced>(wire);
    }
};
}  // namespace fixy::session

int main() { return 0; }
