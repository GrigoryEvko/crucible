// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-068 PART A floor-restoration: second HS14 fixture for
// `mint_computation` to lift the inventory floor from HS14:1 → HS14:2.
//
// Companion to neg_mint_computation_non_pure_row.cpp (which exercises
// `Computation<Row<Bg>, int>::mint_computation`).  THIS fixture
// exercises a DIFFERENT non-empty row — `Row<Effect::IO>` — and proves
// the gate is structurally about `row_size_v<R> == 0`, NOT about any
// particular Effect atom.  The two fixtures together witness the
// gate-firing across the Effect lattice without leaning on a single
// effect-specific failure path.
//
// `mint_computation(T x)` requires `row_size_v<R> == 0`.  An IO-engaged
// Computation has `row_size_v == 1`, so the requires-clause fails and
// the static factory is constraint-rejected at substitution.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at the `requires (row_size_v<R> == 0)` clause.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>

namespace eff = crucible::effects;

int main() {
    // IO-row Computation — mint_computation requires row_size_v<R> == 0,
    // so this template member is constraint-rejected.
    auto bad = eff::Computation<eff::Row<eff::Effect::IO>, int>::mint_computation(7);
    (void)bad;
    return 0;
}
