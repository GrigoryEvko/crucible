// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// compute_fusion_benefit is pure arithmetic over costs the caller already
// has, and CtxFitsCostModel admits only a context whose row is empty.  This
// context claims Block, so the gate refuses the call.  A caller that can
// block narrows its context to the empty row first, and that call compiles.

#include <crucible/CostModel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Block>> block_ctx{eff::testing::bg()};
    (void)::crucible::compute_fusion_benefit(block_ctx, 10.0, 5.0, 64u, 1u);
    return 0;
}
