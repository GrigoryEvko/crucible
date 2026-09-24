// The recorder keeps the address of the log.  A temporary log dies at
// the end of the statement, so the recorder would write to a dead
// object.  The log parameter takes an lvalue only.

#include <fixy/session/Recording.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

int main() {
    auto handle = s::mint_session_handle<s::Send<int, s::End>>(Wire{});
    auto recorded = s::mint_recorded_session(std::move(handle), s::SessionEventLog{}, s::RoleTagId{1}, s::RoleTagId{2});
    (void)recorded;
    return 0;
}
