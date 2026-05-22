// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-219 HS14 fixture #2 of 2 for fixy::effect::RowEngagementWitnessed:
// empty-row SEMANTIC rejection — a well-formed Computation<R, T>
// whose row is Row<> (empty, pure value) must reject via the
// effect_count_in_row > 0 clause of RowEngagementWitnessed.
//
// Violation: `RowEngagementWitnessed<C>` requires the row to claim
// at least ONE Effect atom.  A Computation<Row<>, T> represents a
// pure value with no effect claims — legitimate (this is what `mk(x)`
// produces) but explicitly NOT row-engaged.  The concept rejects to
// pin a contract for band-3 stances: if a call site declares
// `requires RowEngagementWitnessed<C>`, it MUST receive a Computation
// with a non-empty row.
//
// Distinct from fixture #1 (IsComputation structural rejection):
//   * Fixture #1 — STRUCTURAL rejection (`int` is not a
//     Computation<...> at all; IsComputation clause rejects).
//   * Fixture #2 — SEMANTIC rejection (Computation<Row<>, int> IS a
//     valid Computation<...>, but its row = Row<> doesn't contain
//     any Effect atom; effect_count_in_row > 0 clause rejects).
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Background (Agent 8 Bug 7).  The motivating bug pattern is the
// discipline-unfriendly construction:
//
//     auto pure = Computation<Row<>, int>::mk(42);
//     auto bad  = pure.template weaken<Row<Effect::Alloc>>();
//     // bad has type Computation<Row<Alloc>, int> but the body
//     // never actually engaged Alloc — the row claim is structural,
//     // not substantive.
//
// V-219 closes the hole on TWO sides:
//   * Substrate (Computation.h:210-237) — `weaken<R2>` now requires
//     `(row_size_v<R> > 0 || row_size_v<R2> == 0)` so the FIRST
//     expression (`pure.weaken<Row<Alloc>>()`) reds at compile time.
//   * Band-3 (this concept) — call sites declaring
//     `requires RowEngagementWitnessed<C>` reject any Computation
//     whose row is Row<>, so even if someone bypassed the substrate
//     guard (e.g. via an unconstrained T-taking ctor), the band-3
//     stance catches it at the consuming gate.
//
// Defense-in-depth: substrate forbids the unsound CONSTRUCTION,
// band-3 forbids the unsound CONSUMPTION.  Fixture #2 pins the
// band-3 axis with a CONSTRUCTION that the substrate ALLOWS (pure
// Computation<Row<>, T>{x}) but the band-3 stance REJECTS at use.
//
// Expected diagnostic: static assertion failed mentioning
// RowEngagementWitnessed (concept unsatisfied because
// effect_count_in_row() = 0 for the empty row).

#include <crucible/fixy/Effect.h>

int main() {
    // Computation<Row<>, int> is a pure-value Computation — well-
    // formed, legitimately constructible via `mk(x)` or the
    // explicit T-taking ctor.  Its `effect_count_in_row()` returns
    // 0 because Row<> has zero atoms.  The concept rejects.  This
    // pins the row-engagement axis: the concept enforces what the
    // Computation CLAIMS in its row, independent of what the body
    // could have done.  If the concept ever drifted to admit empty
    // rows (e.g. via a stale `effect_count_in_row >= 0` check that
    // is always true), this fixture would silently compile and a
    // band-3 site declaring `requires RowEngagementWitnessed<C>`
    // could accept stray pure-row Computations that hide the
    // weaken-from-empty pattern of Agent 8 Bug 7.
    using PureComputation = ::crucible::effects::Computation<
        ::crucible::effects::Row<>, int>;
    static_assert(::crucible::fixy::effect::RowEngagementWitnessed<PureComputation>,
        "FIXY-V-219 fixture #2: Computation<Row<>, int> has empty row — "
        "RowEngagementWitnessed must reject via effect_count_in_row > 0.");
    return 0;
}
