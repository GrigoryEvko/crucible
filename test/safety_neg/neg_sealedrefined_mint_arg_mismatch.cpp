// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `mint_sealed_refined<non_null, int>(...)` —
// the predicate `non_null` has signature `auto* p` (REQUIRES p
// to deduce as a pointer type), but T is `int` (non-pointer).
// `PredicateInvocableOn<non_null, int>` is unsatisfied — Pred
// cannot be invoked with an int argument because template
// deduction of `auto* p` against int fails before the body runs.
//
// Discipline rationale (SealedRefined.h, mirrors Refined.h):
//   The §XXI mint factory's load-bearing gate is
//   `PredicateInvocableOn<Pred, T>` — the value T MUST be a
//   valid argument to the chosen predicate.  A mismatched pair
//   like `<non_null, int>` is a category error: int has no null
//   to check against; the predicate semantics simply don't
//   compose.
//
//   Without this gate, the user gets a deeply nested
//   "substitution failure" error from inside the contract
//   pre-clause's evaluation; with the concept gate, the
//   diagnostic is clean and points at the call site.
//
//   This is the SAME predicate-invocability discipline that
//   protects Refined<P, T>; SealedRefined inherits it directly.
//   The shared concept (defined in safety/Refined.h) is the
//   reuse point.
//
// HS14 — paired with neg_sealedrefined_into_absent for distinct
// mismatch classes:
//   * Class T (sibling):    structural method-absence rejection
//     on the wrapper's distinguishing API surface.
//   * Class U (THIS file):  PredicateInvocableOn concept-gate
//     rejection at the §XXI mint factory.
// Together the pair pins both soundness layers of SealedRefined:
//   (a) structural sealing (no into() escape hatch); and
//   (b) predicate-invocability gate (mint refuses categorically
//       mismatched (Pred, T) pairs).
//
// U-143 — Class U fixture (closes SealedRefined slice of #146 A8-P2).

#include <crucible/safety/SealedRefined.h>

#include <cstdint>

// Anchor a legitimate call so the file is self-contained — int
// is a valid arg for the `positive` predicate (templated
// `auto x`, requires `> 0`).  This call compiles.
[[maybe_unused]] static auto anchor_mint_with_invocable_pair() {
    return ::crucible::safety::mint_sealed_refined<
        ::crucible::safety::positive, int>(7);
}

// VIOLATION: non_null's signature is `auto* p` — requires p to
// deduce as a pointer.  Passing int as T fails template
// deduction of `auto*` against int.  The `PredicateInvocableOn
// <non_null, int>` concept (defined in safety/Refined.h, used
// by mint_sealed_refined's requires-clause) is unsatisfied.
// GCC rejects with "constraints not satisfied" naming the
// PredicateInvocableOn concept.
[[maybe_unused]] static auto offending_mint_non_null_on_int() {
    return ::crucible::safety::mint_sealed_refined<
        ::crucible::safety::non_null, int>(42);  // ERROR: non_null wants pointer
}

int main() { return 0; }
