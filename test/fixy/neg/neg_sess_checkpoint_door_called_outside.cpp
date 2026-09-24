// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The checkpoint door opens the first checkpoint handle of a session.  A
// call from a scope that is not the mint does not do the compliance check
// of the mint.  The member of the door is private.  The compiler rejects
// the call, also for a pair of protocols that the mint accepts.
//
// Expected diagnostic: the door member is private in this context.

#include <fixy/session/Checkpoint.h>

#include <source_location>

namespace neg_sess_checkpoint_door_called_outside_types {
struct Wire {};
}  // namespace neg_sess_checkpoint_door_called_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_door_called_outside_types;
    using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;
    using Follow = s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>;
    auto forged =
        s::CheckpointDoor::open_<Decide, Follow, s::DefaultAbandonmentPolicy>(Wire{}, std::source_location{});
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
