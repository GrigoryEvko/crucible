// A delegated endpoint arrives in a crash-aware session.  The delegated
// protocol receives from a peer that no detector of this session watches,
// so a crash of that peer leaves the holder waiting.  The crash-stop
// theory has no delegation, and the mint refuses the payload.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;
namespace fp = foundation::permissions;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Inner = s::DelegatedSession<s::Recv<int, s::End>, fp::EmptyPermSet>;
using Proto = s::Offer<s::Recv<Inner, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
