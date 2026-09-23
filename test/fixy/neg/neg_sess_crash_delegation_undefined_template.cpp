// A crash session whose payload points at a specialization of a template
// that is only declared here.  The delegation query cannot read what the
// specialization holds, and a unit that defines the template would read
// it, so the query stops the build instead of giving a value.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};

template <typename T>
struct OpaqueChannel;

using Proto = s::Offer<s::Recv<OpaqueChannel<int>*, s::End>, s::Recv<s::Crash<Bob>, s::End>>;
}  // namespace

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
