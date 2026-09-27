// A pointer to a session handle hides in a member of the payload.  The
// recipient can step the handle through the pointer.  The pointer then
// delegates the endpoint, as the handle itself does.  The payload walk
// refuses it, so the permission flow of the mint does not close.

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
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
