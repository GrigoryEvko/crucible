// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// sm_occupancy is pure arithmetic, and CtxFitsCostModel admits only a
// context whose row is empty.  This context claims Alloc, so the gate
// refuses the call.  A caller that can allocate narrows its context to the
// empty row first, and that call compiles.

#include <crucible/CostModel.h>

#include <fixy/Refined.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstdint>

namespace eff = ::foundation::effects;

int main() {
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Alloc>> alloc_ctx{eff::testing::bg()};
    auto hw = ::crucible::blackwell_b200();
    const auto regs = ::fixy::mint_refined<::crucible::valid_regs_per_thread>(std::uint16_t{32});
    (void)::crucible::sm_occupancy(alloc_ctx, regs, 0u, std::uint16_t{8}, hw);
    return 0;
}
