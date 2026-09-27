// The delegated endpoint hides in a member of the payload.  The component
// walk reads the members of the payload, so the mint refuses it as it
// refuses a delegated endpoint sent alone.

#include <fixy/session/CrashTransport.h>
#include <fixy/session/Delegate.h>

namespace s = fixy::session;
namespace fp = foundation::permissions;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
struct Envelope {
    int sequence = 0;
    s::DelegatedSession<s::Recv<int, s::End>, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet> inner;
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
