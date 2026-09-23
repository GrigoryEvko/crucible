// A reception with no crash branch is found inside a loop body, after a
// guarded reception on the first iteration.  The walk reaches every
// position, so the crash-aware mint refuses the protocol.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Loop<s::Offer<s::Recv<int, s::Send<int, s::Recv<int, s::Continue>>>, s::Recv<s::Crash<Bob>, s::End>>>;

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
