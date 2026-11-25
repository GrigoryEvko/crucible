// Negative compile test.  This file must fail to compile.
//
// An append to a log can allocate.  So the mint of the writer takes the
// allocation capability, and a tag of a different effect pays for no append.
//
// Expected diagnostic: no mint takes the tag of IO.

#include <fixy/session/EventLog.h>
#include <foundation/effects/Effect.h>

int main() {
    namespace s = ::fixy::session;
    s::SessionEventLog log;
    s::SessionEventWriter writer = s::mint_session_event_writer(::foundation::effects::testing::test().io, log);
    writer.record_now(s::SessionEvent::close(s::RoleTagId{1}, s::RoleTagId{2}));
    return 0;
}
