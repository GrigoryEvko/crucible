// mint_session_handle refuses a protocol holding a reachable empty
// choice, even when the empty choice is nested rather than at the top.
//
// Select<> is a legitimate operand in subtyping, which is why the trait
// keeps admitting it; it is a runnable handle that cannot exist, so the
// rejection lives at the handle boundary.  Catching it at mint time is
// the point: the alternative is a build that succeeds and then fails at
// the eventual select<I>() that has no branch to reach.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Ping {};
struct Wire {};
}  // namespace

// Well-formed — every Continue has its Loop, because there is none —
// and still not runnable.  Only the empty-choice clause refuses this.
using DeadEnd = s::Send<Ping, s::Select<>>;

int main() {
    auto handle = s::mint_session_handle<DeadEnd, Wire>(Wire{});
    (void)handle;
    return 0;
}
