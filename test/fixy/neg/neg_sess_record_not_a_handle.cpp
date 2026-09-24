// The recorder wraps a session handle.  A value that names no protocol
// and no Resource is not one, so there is nothing to record.

#include <fixy/session/Recording.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

int main() {
    s::SessionEventLog log;
    auto recorded = s::mint_recorded_session(42, log, s::RoleTagId{1}, s::RoleTagId{2});
    (void)recorded;
    return 0;
}
