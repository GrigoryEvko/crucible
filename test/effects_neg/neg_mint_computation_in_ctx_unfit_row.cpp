// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-077: §XXI Universal Mint Pattern applied to Computation
// (Ctx-bound mint leg).
//
// `mint_computation_in_ctx<Cap, Ctx>(ctx, x)` is the §XXI Ctx-bound
// alias of `lift_in<Cap, Ctx>(ctx, x)`.  The requires-clause
//
//     IsEffect<Cap>
//   ∧ IsExecCtx<Ctx>
//   ∧ row_contains_v<Ctx::row_type, Cap>
//
// is the rigorous closure FIXY-FOUND-014 established for `lift_in`.
// The alias inherits the gate; calling
// `mint_computation_in_ctx<Effect::Bg>(hot_fg_ctx, x)` must fire the
// third predicate because HotFgCtx's row_type is Row<> (empty),
// which does not contain Bg.
//
// Pairs with neg_mint_computation_non_pure_row.cpp for the §XXI HS14
// floor (2 distinct mismatch classes).
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at the `row_contains_v<Ctx::row_type, Cap>` clause.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    eff::HotFgCtx fg;  // row_type = Row<> (empty)

    // mint_computation_in_ctx<Bg>(fg, ...) requires Ctx::row_type to
    // contain Bg.  Empty row does not contain Bg → requires-clause
    // fails, alias function template is constraint-rejected.
    auto bad = eff::Computation<eff::Row<>, int>::
                   mint_computation_in_ctx<eff::Effect::Bg>(fg, 42);
    (void)bad;
    return 0;
}
