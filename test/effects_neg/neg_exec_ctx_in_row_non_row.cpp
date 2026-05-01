// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT (#852): in_row<NewRow>() Row-shape concept gate.
//
// Violation: ExecCtx<>{}.in_row<int>() — `int` is not a recognised
// effect-row.  The builder method's `requires IsEffectRow<NewRow>`
// rejects this before the Subrow / cap-coherence checks even fire,
// giving the user a clean "not a Row" diagnostic.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at the IsEffectRow clause on in_row<>().

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    constexpr auto bad = eff::ExecCtx<>{}.in_row<int>();
    (void)bad;
    return 0;
}
