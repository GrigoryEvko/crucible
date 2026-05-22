// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-219 HS14 fixture #1 of 2 for fixy::effect::RowEngagementWitnessed:
// IsComputation STRUCTURAL rejection — passing a type that is not a
// Computation<R, T> instantiation as the C template parameter must
// reject via the IsComputation clause of RowEngagementWitnessed.
//
// Violation: `RowEngagementWitnessed<C>` is defined as
//
//     IsComputation<std::remove_cvref_t<C>>
//       && (std::remove_cvref_t<C>::effect_count_in_row() > 0)
//
// The IsComputation<C> clause is hard-checked structurally — C must
// be a Computation<R, T> template instantiation, and a plain
// non-Computation type cannot satisfy the concept.  The short-circuit
// evaluation order guarantees `effect_count_in_row()` is NEVER reached
// when IsComputation = false (which would hard-error on the
// member-function lookup for a non-Computation type) — clean
// rejection diagnostic.
//
// Distinct from fixture #2 (semantic empty-row rejection):
//   * Fixture #1 — STRUCTURAL rejection at the IsComputation clause.
//     C is not a Computation<...> at all.  No row to query.
//   * Fixture #2 — SEMANTIC rejection at the effect_count_in_row
//     clause.  C IS a valid Computation<...>, but its row is
//     Row<> (empty, no engagement).
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Background (Agent 8 Bug 7).  The motivating bug pattern is the
// discipline-unfriendly path of constructing `Computation<Row<>, T>`
// (pure) and `weaken<Row<Effect::Alloc>>`-ing it to claim Alloc
// without ever demonstrating Alloc in the body.  The substrate-level
// guard on `weaken<R2>` (Computation.h:210-237) rejects that
// EXPRESSION; this band-3 concept surfaces the row-engagement claim
// at the TYPE level so band-3 stances can require it.  Fixture #1
// closes the "not even a Computation" hole — a band-3 site that
// claims `requires RowEngagementWitnessed<C>` would compile-error
// here, forcing the call site to take an actual Computation result
// type.
//
// Expected diagnostic: static assertion failed mentioning
// RowEngagementWitnessed / IsComputation (the concept is unsatisfied
// because `int` is not a Computation<...>).

#include <crucible/fixy/Effect.h>

int main() {
    // `int` is not a Computation<...> template instantiation.  The
    // IsComputation<int> clause rejects; the concept evaluates to
    // false; the static_assert fires with a grep-discoverable
    // diagnostic.  If the concept ever drifted to admit non-
    // Computation types (e.g. via a duck-typed `requires { typename
    // T::row_type; }` weakening), this fixture would silently
    // compile and a band-3 site could pass a stray int through
    // a row-engagement gate.
    static_assert(::crucible::fixy::effect::RowEngagementWitnessed<int>,
        "FIXY-V-219 fixture #1: int is not a Computation — "
        "RowEngagementWitnessed must reject via IsComputation structural check.");
    return 0;
}
