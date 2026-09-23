// mint_session_handle refuses a protocol holding a reachable empty
// choice, even when the empty choice is nested rather than at the top.
//
// An empty choice is not well-formed, and the mint gate states the
// empty-choice clause first, so the refusal names that fault.  Catching
// it at mint time is the point: the alternative is a build that succeeds
// and then fails at the eventual select<I>() that has no branch to reach.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Ping {};
struct Wire {};
}  // namespace

// Every Continue has its Loop, because there is none, so the only fault
// is the empty choice below the Send.
using DeadEnd = s::Send<Ping, s::Select<>>;

int main() {
    auto handle = s::mint_session_handle<DeadEnd, Wire>(Wire{});
    (void)handle;
    return 0;
}
