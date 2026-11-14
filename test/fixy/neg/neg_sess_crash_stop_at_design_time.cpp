// Stop is the runtime type of a crashed endpoint (LMCS 2025 Fig. 6).  A
// protocol written at design time never contains it.

#include <fixy/Ctx.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Entry.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Send<int, s::Stop>;

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Proto>(ctx, Wire{});
    (void)handle;
    return 0;
}
