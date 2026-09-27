// A session handle arrives in a crash-aware session.  The handle has a
// peer of its own that no detector of this session watches.  A crash of
// that peer leaves the holder waiting.  The payload walk refuses a bare
// endpoint in every session, so the permission flow of the mint does not
// close, and the mint refuses the protocol.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using PeerEnd = s::SessionHandle<s::Recv<int, s::End>, Wire>;
using Proto = s::Offer<s::Recv<PeerEnd, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
