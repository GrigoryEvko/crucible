// A session event built from bytes that no session step wrote.  The copy
// constructor of SessionEvent is user-provided, so the event is not
// trivially copyable, and std::bit_cast has no candidate.  The one route
// from bytes to an event is decode_session_event, which checks each byte.

#include <fixy/session/EventLog.h>

#include <array>
#include <bit>
#include <cstddef>

namespace s = ::fixy::session;

int main() {
    const std::array<std::byte, s::session_event_size> bytes{};
    auto forged = std::bit_cast<s::SessionEvent>(bytes);
    (void)forged;
    return 0;
}
