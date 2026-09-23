// A choice whose every branch is a crash branch has no label the peer
// can send (rule 2), so it is an empty choice and no handle exists for
// it.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Send<int, s::Offer<s::Recv<s::Crash<Bob>, s::End>>>;

int main() {
    auto handle = s::mint_session_handle<Proto, Wire>(Wire{});
    (void)handle;
    return 0;
}
