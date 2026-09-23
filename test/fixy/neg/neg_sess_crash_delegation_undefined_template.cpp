// A crash session whose payload names a specialization of a template that
// is only declared here.  The delegation query reads a class that a
// template argument names, and it cannot read this one.  A unit that
// defines the template would read it, so the query stops the build
// instead of giving a value.  The payload holds nothing behind a pointer,
// so the permission walk admits it and the delegation query is the gate
// that refuses.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};

template <typename T>
struct OpaqueChannel;

template <typename T>
struct Names {};

using Proto = s::Offer<s::Recv<Names<OpaqueChannel<int>>, s::End>, s::Recv<s::Crash<Bob>, s::End>>;
}  // namespace

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
