// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The door of the recording mint puts a recorder around a handle.  A
// call from a scope that is not the mint does not do the check of the
// mint.  The member of the door is private.  The compiler rejects the
// call, also for a handle that the mint accepts.
//
// Expected diagnostic: the door member is private in this context.

#include <fixy/session/Recording.h>

namespace neg_sess_record_door_called_outside_types {
struct Wire {};
}  // namespace neg_sess_record_door_called_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_record_door_called_outside_types;
    s::SessionEventLog log;
    auto handle = s::mint_session_handle<s::End, Wire>(Wire{});
    auto forged = s::RecordingDoor::make_<decltype(handle)>(std::move(handle), log, s::RoleTagId{1}, s::RoleTagId{2});
    static_cast<void>(std::move(forged).close());
    return 0;
}
