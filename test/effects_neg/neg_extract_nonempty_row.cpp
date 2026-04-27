// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-B13 (#602): negative-compile witness for Met(X) effect-row
// substitution-principle enforcement.
//
// Violation: calling .extract() on a Computation whose row is NOT
// empty.  extract() collapses Computation<EmptyRow, T> back to T —
// it requires the row to be empty (the only effect-free state).
// Calling it on Computation<Row<Effect::Bg>, T> means trying to
// observe the value WITHOUT discharging the Bg effect, breaking
// Met(X)'s capability-propagation invariant.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" with the requires-clause "row_size_v<R> == 0" in the
// instantiation context.

#include <crucible/effects/Computation.h>

namespace eff = crucible::effects;

int main() {
    // Construct a Computation that USES the Bg effect.
    auto bg_computation =
        eff::Computation<eff::Row<eff::Effect::Bg>, int>{42};

    // Try to extract() — fires `requires (row_size_v<R> == 0)`.
    // The compiler rejects: row_size_v<Row<Bg>> == 1, not 0.
    auto v = std::move(bg_computation).extract();
    (void)v;

    return 0;
}
