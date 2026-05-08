// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// implies, mismatch class #1: ANTECEDENT TRUE, CONSEQUENT FALSE
// (the only false cell in the implication truth table).
//
// Pins the canonical violation: `implies(true, false) → false`.
// CRUCIBLE_PRE fires `__builtin_trap()` at consteval, which the
// front-end rejects as "non-constant condition".
//
// Witness `(antecedent = true, consequent = false)`.  This is the
// SOLE false cell in the material-implication truth table, so any
// fixture that violates the predicate must use this combination.
// A buggy "always-true" implementation would COMPILE this fixture;
// a buggy "wrong-direction" (q → p instead of p → q) implementation
// would also COMPILE it.  The correct implementation rejects.
//
// In production this bug manifests as: a guarded conditional
// invariant `pre (decide::implies(X, Y))` admits the case where X
// holds but Y does not — the very scenario the precondition is
// meant to forbid.  The kernel runs in a state where the guard
// fired without its required consequence; the bug surfaces
// downstream as a violated invariant at the use site (e.g. reading
// a content_hash that is supposed to be non-zero, dispatching to
// a Cipher tier whose path field is supposed to be set).
//
// The bug is sneaky because:
//
//   1. Material implication's truth table is NOT symmetric: the
//      single rejecting cell (T, F) is easy to overlook in code
//      review.  A reviewer checking "does it accept (T, T)?" and
//      "does it accept (F, F)?" gets two correct answers under
//      both correct AND buggy impls.
//   2. A WRONG-DIRECTION buggy impl `return !consequent ||
//      antecedent;` (encodes q → p instead of p → q) accepts
//      (T, F) wrongly: !consequent (=!F=T) || antecedent (=T) =
//      true.  This fixture catches it.  An always-true buggy impl
//      `return true;` also accepts (T, F) wrongly — same fixture
//      catches it.
//   3. An IFF-instead-of-implies bug `return antecedent ==
//      consequent;` rejects (T, F) correctly (T != F so returns
//      false).  This fixture would NOT catch that bug; the
//      runtime tests in test_decide.cpp catch it via the (F, T)
//      case (where IFF rejects but correct impl accepts).
//
// Anti-pattern targeted: incomplete implication implementations.
// Specific shapes:
//
//   bool implies(bool a, bool c) { return true; }
//     // ALWAYS-TRUE — defeats the predicate entirely.  This
//     // fixture catches it.
//
//   bool implies(bool a, bool c) { return !c || a; }
//     // WRONG-DIRECTION — encodes c → a (i.e. converse) instead
//     // of a → c.  Truth table:
//     //   a=T, c=T: !T || T = T   (matches correct)
//     //   a=T, c=F: !F || T = T   (correct rejects, bug accepts)
//     //   a=F, c=T: !T || F = F   (correct accepts, bug rejects)
//     //   a=F, c=F: !F || F = T   (matches correct)
//     // This fixture catches the (T, F) divergence.
//
//   bool implies(bool a, bool c) { return a && c; }
//     // AND-INSTEAD-OF-IMPLIES — encodes conjunction.  Truth
//     // table:
//     //   a=T, c=T: T && T = T    (matches correct)
//     //   a=T, c=F: T && F = F    (matches correct — REJECTS!)
//     // This fixture would NOT catch the AND bug (both correct
//     // and buggy impls reject (T, F)).  Caught by runtime
//     // tests on (F, T) where AND returns F but correct returns T.
//
// Distinct from the companion fixture (chained-implication-via-
// computed-antecedent):
//   * direct (this fixture)              — `(true, false)`.  The
//     simplest possible witness against always-true / wrong-
//     direction bugs.
//   * computed-antecedent (companion)    — antecedent computed
//     from another `decide::*` predicate, consequent literal F.
//     Pins that the predicate works in COMPOSITIONAL VC discharge
//     (the typical production-cite shape), not just in isolated
//     literal-constant calls.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for implies: a pure-literal evaluator that special-cases
// constant-true/false would pass this fixture but fail the
// companion (which feeds it a non-trivial computed boolean); a
// wrong-direction implementation fails both with the same
// diagnostic shape.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

namespace {

[[nodiscard]] constexpr bool gate(bool antecedent, bool consequent) noexcept {
    CRUCIBLE_PRE(crucible::decide::implies(antecedent, consequent));
    return true;
}

// `(antecedent=true, consequent=false)` — the unique rejecting cell
// of the material-implication truth table.  Predicate returns
// false; CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches
// always-true and wrong-direction (converse) buggy implementations.
constexpr auto witness = gate(true, false);

}  // namespace

int main() { return 0; }
