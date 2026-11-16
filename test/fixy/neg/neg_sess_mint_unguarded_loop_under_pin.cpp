// A vendor pin around a Loop does not guard the Continue of its body.  The
// handle removes the pin before it enters the Loop, so the mint reads the
// Loop under the pin, and it refuses this loop as it refuses
// Loop<Continue>.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Spin = s::VendorPinned<s::VendorBackend::NV, s::Loop<s::Continue>>;

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Spin>(ctx, Wire{});
    (void)handle;
    return 0;
}
