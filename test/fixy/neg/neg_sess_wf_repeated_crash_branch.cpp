// Two crash branches of one Offer receive the same payload, so the crash
// that enters one of them is ambiguous.  The protocol is not
// well-formed, so no handle is minted for it.

#include <fixy/session/Crash.h>
#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Peer {};
struct Ping {};
struct Wire {};
}  // namespace

using TwoRecoveries = s::Offer<s::Recv<Ping, s::End>, s::Recv<s::Crash<Peer>, s::End>,
                               s::Recv<s::Crash<Peer>, s::Send<Ping, s::End>>>;

int main() {
    auto handle = s::mint_session_handle<TwoRecoveries, Wire>(Wire{});
    (void)handle;
    return 0;
}
