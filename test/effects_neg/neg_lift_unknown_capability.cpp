// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-B13 (#602): negative-compile witness for Met(X) lift<Cap>
// IsEffect concept gate.
//
// Violation: lift<Cap>(T) requires `IsEffect<Cap>` — Cap must be one
// of the recognised Effect atoms.  Passing a non-Effect template
// argument (e.g., int) is rejected at the requires-clause, preventing
// typos like `lift<eff::Effect::AlloC>(...)` that would otherwise
// silently construct a Computation with an unintended row.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at the `requires IsEffect<Cap>` clause.

#include <crucible/effects/Computation.h>

namespace eff = crucible::effects;

int main() {
    // Compile error: `42` is not an Effect atom.
    auto bad = eff::Computation<eff::Row<eff::Effect::Bg>, int>::
        template lift<static_cast<eff::Effect>(99)>(7);
    (void)bad;
    return 0;
}
