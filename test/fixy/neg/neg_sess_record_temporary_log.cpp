// The writer of a log keeps the address of the log.  A temporary log dies
// at the end of the statement, so the recorder would write to a dead
// object.  The mint of the writer refuses a temporary log.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Recording.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<s::Send<int, s::End>>(ctx, Wire{});
    auto recorded = s::mint_recorded_session(std::move(handle), s::mint_session_event_writer(s::SessionEventLog{}),
                                             s::RoleTagId{1}, s::RoleTagId{2});
    (void)recorded;
    return 0;
}
