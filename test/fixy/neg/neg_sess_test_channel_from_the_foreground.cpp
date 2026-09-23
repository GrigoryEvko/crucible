// The second production context the test hatch refuses: the foreground
// context of dispatch.  It holds no Test capability either, so a
// foreground thread cannot get the two endpoints of one channel.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

namespace {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Msg {};
struct Wire {};
using FgCtx = eff::detail::ctx_witnesses::FgWitness;
}  // namespace

int main() {
    auto both = s::mint_test_channel<s::Recv<Msg, s::End>>(FgCtx{}, Wire{}, Wire{});
    (void)both;
    return 0;
}
