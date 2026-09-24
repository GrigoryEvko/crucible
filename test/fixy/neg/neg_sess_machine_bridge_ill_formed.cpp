// The bridge mints session handles over the machine it owns, so a
// protocol it could never run is refused at the bridge's own door
// rather than at the first session_view() call.
//
// Continue binds to the nearest enclosing Loop.  With no Loop above it
// there is no body to return to, so the protocol has no next position.

#include <fixy/session/MachineBridge.h>

namespace s = fixy::session;

namespace {
struct Report {};
struct Payload {
    int ticks = 0;
};
}  // namespace

using Orphan = s::Send<Report, s::Continue>;

int main() {
    auto bridge = s::mint_session_from_machine<Orphan, Payload>();
    (void)bridge;
    return 0;
}
