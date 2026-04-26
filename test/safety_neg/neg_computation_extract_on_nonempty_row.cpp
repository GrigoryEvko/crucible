// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Computation::extract() on a row that contains at
// least one Effect atom.  The METX-1 (#473) `extract()` method
// carries `requires (row_size_v<R> == 0)` — collapsing back to a
// pure value is only sound when no effect remains to discharge.
//
// The diagnostic carries the substring "deferred" or "constraint"
// (depending on whether GCC pattern-matches the requires-clause as a
// constraint failure or a deletion-of-deduction).  We match against
// "extract" which appears in both flavors.
//
// This test pins the contract for the Met(X) effect-row substrate
// (Tang-Lindley POPL 2026, 25_04_2026.md §3): a Computation whose row
// names any effect cannot be unwrapped without first discharging the
// effect via a handler.  If a future edit accidentally relaxes the
// requires-clause (e.g. by allowing Subrow<R, EmptyRow> instead of
// strict equality, or by dropping the constraint entirely), the type
// system would let through "I have a Bg-effect computation, give me
// the value" calls that bypass the foreground-vs-background discipline
// CLAUDE.md §IX rests on.
//
// Companion to neg_computation_weaken_non_subrow.cpp (which exercises
// the substitution-principle direction).
//
// Task #146 (A8-P2 Neg-compile coverage); see
// include/crucible/effects/Computation.h METX-1 #473.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

using namespace crucible::effects;

int main() {
    // Construct a Bg-effect Computation via lift.
    auto bg = Computation<Row<>, int>::lift<Effect::Bg>(42);

    // Should FAIL: extract() requires (row_size_v<R> == 0); the row
    // here is Row<Effect::Bg> with size 1.  The diagnostic should
    // mention "extract" and "constraints not satisfied".
    return bg.extract();
}
