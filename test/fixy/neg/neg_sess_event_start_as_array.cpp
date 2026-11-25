// A session event is not started over bytes.  The copy of an event is trivial,
// so the event is an implicit-lifetime type.  Its seal carries
// no_start_over_bytes, so the checked lifetime start refuses it.

#include <fixy/session/EventLog.h>
#include <foundation/core/Array.h>

namespace s = ::fixy::session;

int main() {
    alignas(s::SessionEvent) unsigned char bytes[s::session_event_size]{};
    auto const events = ::foundation::lifetime::start_as_array<s::SessionEvent>(bytes, 1);
    return static_cast<int>(events[0].op());
}
