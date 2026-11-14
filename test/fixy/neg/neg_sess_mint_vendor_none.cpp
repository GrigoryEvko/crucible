// VendorBackend::None names no kernel, so a session pinned to it runs
// against nothing.  The value filter of the VendorPinned registration
// refuses it at the gate.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Ping {};
struct Wire {};
}  // namespace

using Nowhere = s::VendorPinned<s::VendorBackend::None, s::Send<Ping, s::End>>;

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Nowhere>(ctx, Wire{});
    (void)handle;
    return 0;
}
