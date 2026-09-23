// mint_test_channel gives the two endpoints to one caller, so its gate
// admits only a context that holds the Test capability.  A background
// context holds Bg and no Test, so the gate refuses it.  This is the
// public path that the hatch leaves closed.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

namespace {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Msg {};
struct Wire {};
using DrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    const DrainCtx ctx{eff::testing::bg()};
    auto both = s::mint_test_channel<s::Send<Msg, s::End>>(ctx, Wire{}, Wire{});
    (void)both;
    return 0;
}
