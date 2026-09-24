// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The delegation door builds a DelegatedSession from a handle.  Its only
// friend is mint_delegated_session, and its member is private, so no
// other scope reaches it.
//
// Expected diagnostic: the door member is private in this context.

#include <fixy/session/Delegate.h>

#include <utility>

namespace neg_sess_delegation_door_called_outside_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_delegation_door_called_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_delegation_door_called_outside_types;
    auto handle = s::mint_session_handle<s::Send<int, s::End>, Wire>(Wire{});
    auto carried = s::DelegationDoor::give_(std::move(handle));
    return carried.holds_endpoint() ? 0 : 1;
}
