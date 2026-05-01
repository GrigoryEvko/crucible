// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-4 (#855): mint_from_ctx<E> with a foreground Ctx is
// rejected by CtxCanMint<Ctx, E>.
//
// Violation: HotFgCtx has cap_type=ctx_cap::Fg whose permitted_row
// is empty.  CtxCanMint<HotFgCtx, Effect::Alloc> = false.  The
// requires-clause on mint_from_ctx fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxCanMint.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    eff::HotFgCtx fg;
    auto bad = eff::mint_from_ctx<eff::Effect::Alloc>(fg);  // CtxCanMint fails
    (void)bad;
    return 0;
}
