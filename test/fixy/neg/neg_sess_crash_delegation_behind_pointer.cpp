// A pointer to a session handle hides in a member of the payload.  The
// recipient can step the handle through the pointer.  The pointer then
// delegates the endpoint, as the handle itself does.  The mint refuses
// it.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
using PeerEnd = s::SessionHandle<s::Recv<int, s::End>, Wire>;
struct Envelope {
    int sequence = 0;
    PeerEnd* peer_end = nullptr;
};
}  // namespace

using Proto = s::Offer<s::Recv<Envelope, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
