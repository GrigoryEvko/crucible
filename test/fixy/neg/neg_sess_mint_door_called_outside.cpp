// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The door of the session mints opens a session on the handle factory.
// Each mint states its gate before it calls the door.  A call from a
// scope that is not a friend would skip that gate, and the door states
// only the part of it that every mint shares.  The members of the door
// are private.
//
// Expected diagnostic: the door member is private in this context.

#include <fixy/session/Handle.h>

#include <source_location>

namespace neg_sess_mint_door_called_outside_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_mint_door_called_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_mint_door_called_outside_types;
    auto head = s::SessionMintDoor::open_<s::Send<int, s::End>, Wire, s::DefaultAbandonmentPolicy,
                                          ::foundation::permissions::EmptyPermSet>(Wire{}, std::source_location{});
    static_cast<void>(head);
    return 0;
}
