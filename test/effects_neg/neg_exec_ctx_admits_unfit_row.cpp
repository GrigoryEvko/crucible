// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-2 (#853): CtxAdmits substitution-principle witness.
//
// Violation: a function constrained by `CtxAdmits<Ctx, R>` must
// reject any Ctx whose row does not absorb R.  Here HotFgCtx
// (Row<>) is asked to admit a body of row Row<Effect::Bg> — Bg
// is not a subset of empty, so the requires-clause fails.
//
// Expected diagnostic: `associated constraints are not satisfied`
// pointing at CtxAdmits / Subrow.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

template <eff::IsExecCtx Ctx>
    requires eff::CtxAdmits<Ctx, eff::Row<eff::Effect::Bg>>
constexpr void requires_bg_row(Ctx const&) noexcept {}

int main() {
    eff::HotFgCtx fg;
    requires_bg_row(fg);  // Fg's row is Row<>; cannot admit Row<Bg>
    return 0;
}
