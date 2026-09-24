// A raw pointer is a copyable Resource that reaches a channel, so a copy
// of it is a second channel to the peer.  The handle refuses it.  A
// channel that must stay in place is a foundation::Pinned object that
// the handle holds by lvalue reference.
//
// The protocol is well-formed and moves no permission, so only the
// SessionResource clause can refuse this call.

#include <fixy/session/Handle.h>

namespace s = ::fixy::session;

namespace resource_pointer_fixture {
struct Ping {};
struct Queue {
    int slot = 0;
};
}  // namespace resource_pointer_fixture

using Once = s::Send<resource_pointer_fixture::Ping, s::End>;

int main() {
    resource_pointer_fixture::Queue queue{};
    auto handle = s::mint_session_handle<Once, resource_pointer_fixture::Queue*>(&queue);
    (void)handle;
    return 0;
}
