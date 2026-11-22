// Negative compile test.  This file must fail to compile.
//
// A thread that loses the race of Once::call spins until the winner ends,
// with no exit, so the call asks for a context that owns Div.  The
// foreground context of the hot path owns no atom, so it fails the gate.
//
// Expected diagnostic: CtxOwnsCapability of Div is not satisfied for the
// foreground context.

#include <fixy/Ctx.h>
#include <fixy/handle/Once.h>
#include <foundation/effects/Ctx.h>

namespace h = fixy::handle;

int main() {
    const ::fixy::HotFgCtx fg = ::foundation::effects::testing::foreground();
    h::Once once{};
    once.call(fg, []() noexcept {});
    return 0;
}
