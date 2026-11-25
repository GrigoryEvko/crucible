// The recorder wraps a session handle.  A value that names no protocol
// and no Resource is not one, so there is nothing to record.

#include <fixy/session/Recording.h>
#include <foundation/effects/Effect.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

int main() {
    s::SessionEventLog log;
    auto recorded =
        s::mint_recorded_session(42, s::mint_session_event_writer(::foundation::effects::testing::test().alloc, log),
                                 s::RoleTagId{1}, s::RoleTagId{2});
    (void)recorded;
    return 0;
}
