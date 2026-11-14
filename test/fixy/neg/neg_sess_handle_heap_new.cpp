// A new-expression gives a session handle storage that nothing has to
// free.  A handle that is never destroyed never ends its protocol, and
// the peer then waits for a message that does not come.  The handle
// deletes its class-scope operator new, so the expression is refused.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

namespace {
namespace s = ::fixy::session;
struct Msg {};
struct Wire {};
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto* leaked = new auto(s::mint_session<s::Send<Msg, s::End>>(ctx, Wire{}));
    std::move(*leaked).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
