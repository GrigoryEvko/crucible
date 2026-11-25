// A function returns the events of a local log.  The log ends with the frame of
// the function and frees the events, so a read of the result reads freed
// storage.  The rule view-escape follows the prefix to the log and refuses the
// return.

#include <fixy/session/EventLog.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace s = ::fixy::session;

namespace {

s::SessionEventLog::events_type events_of_local_log() {
    s::SessionEventLog log;
    s::SessionEventWriter writer = s::mint_session_event_writer(::foundation::effects::testing::test().alloc, log);
    writer.record_now(s::SessionEvent::close(s::RoleTagId{1}, s::RoleTagId{2}));
    return log.events();
}

}  // namespace

int main() {
    std::size_t const count = events_of_local_log().size();
    return static_cast<int>(count);
}
