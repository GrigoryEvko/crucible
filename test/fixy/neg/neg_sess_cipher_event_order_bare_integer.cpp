// A persistence event takes its place as a CipherEventOrder, the place of a
// count that the store keeps.  A bare integer, such as the nanoseconds of a
// clock reading, is no place, so the factory refuses it.  A clock time in
// the log makes two runs of one computation write two logs.

#include <fixy/session/EventLog.h>

#include <cstdint>

namespace s = fixy::session;

int main() {
    const std::uint64_t clock_reading_ns = 1000000000u;
    const s::SessionEvent event =
        s::SessionEvent::cipher_event(s::SessionOp::StoreCommitted, s::StepId{1}, s::StateHash{2}, clock_reading_ns);
    (void)event;
    return 0;
}
