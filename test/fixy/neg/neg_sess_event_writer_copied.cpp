// Negative compile test.  This file must fail to compile.
//
// A log has one writer.  A copy of the writer is a second writer, and two
// recorders could then append to one log from two threads.
//
// Expected diagnostic: the copy constructor of the writer is deleted, with
// its reason.

#include <fixy/session/EventLog.h>

int main() {
    namespace s = ::fixy::session;
    s::SessionEventLog log;
    s::SessionEventWriter writer = s::mint_session_event_writer(log);
    s::SessionEventWriter second = writer;
    second.record_now(s::SessionEvent::close(s::RoleTagId{1}, s::RoleTagId{2}));
    return 0;
}
