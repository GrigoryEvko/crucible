// A session event built over a buffer.  No constructor of SessionEvent is
// trivial, so the event is not an implicit-lifetime type, and the mandate
// of std::start_lifetime_as refuses it.

#include <fixy/session/EventLog.h>

#include <memory>

namespace s = ::fixy::session;

int main() {
    alignas(s::SessionEvent) unsigned char storage[s::session_event_size]{};
    auto* forged = std::start_lifetime_as<s::SessionEvent>(storage);
    (void)forged;
    return 0;
}
