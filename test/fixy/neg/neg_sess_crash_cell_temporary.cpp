// The decorator keeps the address of the detector cell.  A temporary
// cell dies at the end of the statement, so every later step would
// read a dead object.  The overload for a temporary is deleted.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, s::PeerCrashCell{});
    (void)handle;
    return 0;
}
