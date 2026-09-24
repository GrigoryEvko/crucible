// A message branch after a crash branch has no wire label: the handle
// numbers the branches, and the peer, which cannot send the crash label,
// numbers only the message branches.  The protocol is not well-formed,
// so no handle is minted for it.

#include <fixy/session/Crash.h>
#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Peer {};
struct Ping {};
struct Wire {};
}  // namespace

using CrashFirst = s::Offer<s::Recv<s::Crash<Peer>, s::End>, s::Recv<Ping, s::End>>;

int main() {
    auto handle = s::mint_session_handle<CrashFirst, Wire>(Wire{});
    (void)handle;
    return 0;
}
