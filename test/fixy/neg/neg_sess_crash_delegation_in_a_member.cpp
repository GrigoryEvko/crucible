// The delegated endpoint hides in a member of the payload.  The component
// walk reads the members of the payload, so the mint refuses it as it
// refuses a delegated endpoint sent alone.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;
namespace fp = foundation::permissions;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
struct Envelope {
    int sequence = 0;
    s::DelegatedSession<s::Recv<int, s::End>, fp::EmptyPermSet> inner;
};
}  // namespace

using Proto = s::Offer<s::Recv<Envelope, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
