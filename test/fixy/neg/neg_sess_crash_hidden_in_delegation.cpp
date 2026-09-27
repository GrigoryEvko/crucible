// A delegated endpoint arrives in a crash-aware session.  The delegated
// protocol receives from a peer that no detector of this session watches,
// so a crash of that peer leaves the holder waiting.  The crash-stop
// theory has no delegation, and the mint refuses the payload.

#include <fixy/session/CrashTransport.h>
#include <fixy/session/Delegate.h>

namespace s = fixy::session;
namespace fp = foundation::permissions;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Inner = s::DelegatedSession<s::Recv<int, s::End>, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;
using Proto = s::Offer<s::Recv<Inner, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
