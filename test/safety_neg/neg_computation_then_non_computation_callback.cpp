// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Computation::then(k) with a callback k that
// returns a plain T (or any non-Computation type) instead of a
// Computation<R2, U>.  This is the bind-vs-fmap discriminator: a
// callback returning a plain value belongs in `.map(f)`, not
// `.then(k)`.  Mixing them up would silently work without the
// IsComputation gate (then would just store the plain value as the
// inner_), but the SEMANTICS would diverge — the resulting type
// would claim row_union_t<R, ?> with `?` undefined, and the row
// algebra would fall apart at the next step.
//
// The IsComputation concept (Computation.h, METX-1 #473) requires
// the callback's invoke_result to satisfy detail::is_computation —
// trait specialized only for Computation<R, T>.  Trying to bind a
// `[](int) -> int { ... }` fails the requires-clause cleanly with
// "no matching function" + the IsComputation requirement spelled
// out in the candidate's signature.
//
// This is the type-level analog of "you cannot use map and bind
// interchangeably; the kind of the result distinguishes them."
// Without this gate, the M(X) monadic substrate would be a
// degenerate functor in disguise.
//
// Match token: "then" — the diagnostic mentions the failing method.
//
// Companion to neg_computation_extract_on_nonempty_row.cpp and
// neg_computation_weaken_non_subrow.cpp; together the three pin the
// METX-1 contract surface from extract / weaken / then directions.
//
// Task #146 (A8-P2 Neg-compile coverage); see
// include/crucible/effects/Computation.h METX-1 #473 then.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>

using namespace crucible::effects;

int main() {
    auto pure = Computation<Row<>, int>::mk(42);

    // Should FAIL: then() requires the callback's invoke_result to
    // satisfy IsComputation.  A lambda returning plain int is NOT
    // a Computation, so the requires-clause rejects.  The user
    // should call .map(...) instead.
    auto bad = pure.then([](int x) { return x + 1; });
    (void)bad;
    return 0;
}
