// Negative compile test.  This file must fail to compile.
//
// The body of a frame of a session event log writes its View into a variable of
// the caller.  The next append of the writer can move the events that the View
// borrows.
//
// Expected diagnostic: the overload of the frame door for a refused body is
// deleted, with the reason.

#include <fixy/session/EventLog.h>
#include <foundation/effects/Effect.h>

int main() {
    namespace s = ::fixy::session;
    s::SessionEventLog log;
    s::SessionEventWriter writer = s::mint_session_event_writer(::foundation::effects::testing::test().alloc, log);
    writer.record_now(s::SessionEvent::close(s::RoleTagId{1}, s::RoleTagId{2}));
    ::foundation::core::View<s::SessionEvent const> kept{};
    log.with_view([&kept](::foundation::core::View<s::SessionEvent const> events) noexcept { kept = events; });
    writer.record_now(s::SessionEvent::close(s::RoleTagId{1}, s::RoleTagId{2}));
    return static_cast<int>(kept.size());
}
