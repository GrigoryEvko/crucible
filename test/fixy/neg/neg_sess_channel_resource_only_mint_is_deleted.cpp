// The resource-only channel mint gives the two endpoints of one channel
// to one caller.  A caller that holds the two endpoints can wait on one
// of them for a message that only the other can send.  The form is
// deleted, and the reason names the fork-shaped mint.

#include <fixy/session/Handle.h>

namespace {
namespace s = ::fixy::session;
struct Msg {};
struct Wire {};
}  // namespace

int main() {
    s::mint_channel<s::Send<Msg, s::End>>(Wire{}, Wire{});
    return 0;
}
