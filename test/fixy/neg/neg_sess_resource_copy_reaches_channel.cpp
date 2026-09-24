// A Resource that points at a channel and can be copied gives the holder
// a second channel to the peer.  The holder can send the copy in a
// message, or mint a second handle on it, and write outside the
// protocol.  The handle refuses a copyable Resource that reaches state
// outside itself.  A [[no_unique_address]] fixy::session::MoveOnlyResource
// member makes such a Resource move-only, and the handle then admits it.
//
// The protocol is well-formed and moves no permission, so only the
// SessionResource clause can refuse this call.

#include <fixy/session/Handle.h>

namespace s = ::fixy::session;

namespace resource_copy_fixture {
struct Ping {};
struct Queue {
    int slot = 0;
};
struct SharedWire {
    Queue* queue = nullptr;
};
}  // namespace resource_copy_fixture

using Once = s::Send<resource_copy_fixture::Ping, s::End>;

int main() {
    resource_copy_fixture::Queue queue{};
    auto handle = s::mint_session_handle<Once>(resource_copy_fixture::SharedWire{&queue});
    (void)handle;
    return 0;
}
