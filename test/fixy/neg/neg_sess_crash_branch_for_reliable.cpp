// A crash branch for a reliable sender is untypable: projection adds
// none, and rule Sub-& forbids a subtype to add one (rule 5).  The
// crash-aware mint refuses it, because the declared reliable set and
// the protocol disagree.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob, s::ReliableSet<Bob>>(Wire{}, cell);
    (void)handle;
    return 0;
}
