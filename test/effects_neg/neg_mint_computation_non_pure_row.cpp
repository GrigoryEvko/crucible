// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-077: §XXI Universal Mint Pattern applied to Computation.
//
// `mint_computation(T x)` is the §XXI-named alias of `mk(T x)` — the
// pure-row lift factory.  It requires `row_size_v<R> == 0` (the
// Computation must already be aliased to the empty row at the static
// member's instantiation).  Calling it on a non-empty-row Computation
// must be rejected at the requires-clause.
//
// This fixture instantiates `Computation<Row<Bg>, int>::mint_computation(42)`
// — Bg is a non-empty row, so the gate fires.
//
// Pairs with neg_mint_computation_in_ctx_unfit_row.cpp for the §XXI
// HS14 floor (2 distinct mismatch classes):
//   1. mint_computation: empty-row-required (this).
//   2. mint_computation_in_ctx: ctx-row-must-contain-Cap.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at the `requires (row_size_v<R> == 0)` clause.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>

namespace eff = crucible::effects;

int main() {
    // Bg-row Computation — mint_computation requires row_size_v<R> == 0,
    // so this template member is constraint-rejected.  Result type is
    // never instantiated; the diagnostic fires at substitution.
    auto bad = eff::Computation<eff::Row<eff::Effect::Bg>, int>::mint_computation(42);
    (void)bad;
    return 0;
}
