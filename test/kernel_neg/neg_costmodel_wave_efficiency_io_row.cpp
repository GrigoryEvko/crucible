// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// wave_efficiency is pure arithmetic, and CtxFitsCostModel admits only a
// context whose row is empty.  This context claims IO, so the gate refuses
// the call.  A caller with IO narrows its context to the empty row first,
// and that call compiles.

#include <crucible/CostModel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::IO>> io_ctx{eff::testing::bg()};
    auto hw = ::crucible::blackwell_b200();
    (void)::crucible::wave_efficiency(io_ctx, 1u, hw);
    return 0;
}
