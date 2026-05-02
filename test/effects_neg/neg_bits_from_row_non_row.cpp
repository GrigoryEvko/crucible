// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for effects::EffectRowProjection (#1087).
//
// Premise: bits_from_row<R>() where R is NOT a Row<Es...>
// instantiation MUST fail at the IsEffectRow concept gate.  The
// projection is meaningful only for Row-typed inputs — passing an
// arbitrary type would either silently succeed via implicit coercion
// (defeating type safety) or produce a confusing deep-template-
// substitution error.  The IsEffectRow concept rejects up front.
//
// Expected diagnostic: "associated constraints are not satisfied" /
// "constraints not satisfied" / "no matching function" pointing at
// the bits_from_row<int>() call site.

#include <crucible/effects/EffectRowProjection.h>

namespace eff = crucible::effects;

int main() {
    // Bridge fires: bits_from_row<R>() requires IsEffectRow<R>;
    // R = int does not match the Row<Es...> partial spec, so the
    // concept gate rejects.
    auto bad = eff::bits_from_row<int>();
    (void)bad;
    return 0;
}
