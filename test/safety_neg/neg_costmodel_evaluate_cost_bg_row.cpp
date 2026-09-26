// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// evaluate_cost is pure arithmetic, and CtxFitsCostModel admits only a
// context whose row is empty.  This context claims Bg, the authority of
// the background thread, so the gate refuses the call.  The background
// thread narrows its context to the empty row first, and that call
// compiles.

#include <crucible/CostModel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> bg_ctx{eff::testing::bg()};
    auto hw = ::crucible::blackwell_b200();
    ::crucible::KernelConfig cfg{};
    (void)::crucible::evaluate_cost(bg_ctx, 1u, 1u, 1u, ::crucible::ScalarType::Float, cfg, hw);
    return 0;
}
