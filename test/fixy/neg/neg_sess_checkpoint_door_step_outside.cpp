// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The checkpoint door builds a plain handle at a protocol position that
// the caller names.  A checkpoint handle calls it only at a position that
// the compliance check of its session proved.  A call from another scope
// gets a plain handle at a position that no mint accepted.  The member
// of the door is private.
//
// Expected diagnostic: the door member is private in this context.

#include <fixy/session/Checkpoint.h>

namespace neg_sess_checkpoint_door_step_outside_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_checkpoint_door_step_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_door_step_outside_types;
    auto forged = s::CheckpointDoor::step_<s::End, Wire, void, s::DefaultAbandonmentPolicy>(Wire{});
    return std::move(forged).close().words;
}
