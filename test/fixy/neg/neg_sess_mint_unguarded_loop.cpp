// A loop whose body is its own Continue has no action before the loop
// back, so its unfold never stops.  The guard rule refuses it at the
// gate of mint_session.  Past the gate, the build would fail
// inside the handle unroll with an error that names no rule.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Spin = s::Loop<s::Continue>;

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Spin>(ctx, Wire{});
    (void)handle;
    return 0;
}
