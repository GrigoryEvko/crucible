// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-B13 (#602): negative-compile witness for Met(X) Subrow gate.
//
// Violation: weaken<R2>() requires Subrow<R, R2> — the source row
// must be a SUBSET of the target row.  Calling weaken<Row<IO>>() on
// a Computation<Row<Bg>, T> tries to widen INTO a row that doesn't
// CONTAIN Bg, which would let the holder claim "I only need IO" while
// actually performing Bg work.  That breaks substitution: a context
// holding Row<IO> capability would invoke a function that needs Bg.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" with the requires-clause "Subrow<R, R2>" — the concept
// rejects because Row<Bg> is not a subset of Row<IO>.

#include <crucible/effects/Computation.h>

namespace eff = crucible::effects;

int main() {
    auto bg_computation =
        eff::Computation<eff::Row<eff::Effect::Bg>, int>{42};

    // Try to weaken into Row<IO> — Bg ⊄ {IO}, so Subrow fails.
    auto io_computation =
        std::move(bg_computation).template weaken<eff::Row<eff::Effect::IO>>();
    (void)io_computation;

    return 0;
}
