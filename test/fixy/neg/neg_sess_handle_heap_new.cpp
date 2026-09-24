// A new-expression gives a session handle storage that nothing has to
// free.  A handle that is never destroyed never ends its protocol, and
// the peer then waits for a message that does not come.  The handle
// deletes its class-scope operator new, so the expression is refused.

#include <fixy/session/Handle.h>

namespace {
namespace s = ::fixy::session;
struct Msg {};
struct Wire {};
}  // namespace

int main() {
    auto* leaked = new auto(s::mint_session_handle<s::Send<Msg, s::End>, Wire>(Wire{}));
    std::move(*leaked).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
