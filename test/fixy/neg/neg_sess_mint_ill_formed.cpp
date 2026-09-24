// mint_session_handle refuses a protocol whose Continue has no
// enclosing Loop.  Nothing at runtime could bind that Continue to a
// body, so the handle it would return has no next position.
//
// The rejection is the concept on the signature, not the body
// static_assert: a body assert is invisible to SFINAE, and a caller
// asking "can this be minted" needs an answer rather than a hard error.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Ping {};
struct Wire {};
}  // namespace

// Continue is well-formed only under a Loop.  Send<Ping, Continue> at
// top level has none.
using Orphan = s::Send<Ping, s::Continue>;

int main() {
    auto handle = s::mint_session_handle<Orphan, Wire>(Wire{});
    (void)handle;
    return 0;
}
