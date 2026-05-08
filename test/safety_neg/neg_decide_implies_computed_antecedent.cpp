// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// implies, mismatch class #2: COMPOSITIONAL VC DISCHARGE — the
// antecedent is computed from another `decide::*` predicate, the
// consequent is literal `false`.
//
// Pins the predicate's correctness when used in the canonical
// production-cite shape: a guarded conditional invariant of the
// form "if [some other predicate holds] then [consequent must
// hold]".  This is how `implies` is typically cited in production
// (CONTRACT-106 Cipher publish gate, CONTRACT-111 Forge recipe
// gate, CONTRACT-119 Cipher state-machine invariant).
//
// Witness: antecedent = `decide::is_power_of_two_le(8u, 64u)`
// (returns true; 8 is a power of 2 less-or-equal to 64),
// consequent = `false`.  Predicate evaluates to
// `implies(true, false) = false`.  CRUCIBLE_PRE fires
// `__builtin_trap()` at consteval; front-end rejects as
// "non-constant condition".
//
// In production this bug manifests as: a publish gate of the form
// `pre (decide::implies(decide::is_power_of_two_le(slot, MAX),
//                       slot_metadata.is_set))` admits the case
// where the slot is a valid power of two but its metadata is
// uninitialised — the very scenario the precondition is meant to
// forbid.  The kernel runs with stale metadata; the bug surfaces
// downstream as wrong-cache-hit or use-of-zero-content_hash.
//
// The bug is sneaky because:
//
//   1. Pure-literal evaluators of `implies` (a buggy impl that
//      special-cases the four truth-table cells with bool literals)
//      could miss the case where the antecedent is itself a
//      function call returning a bool.  The compiler MUST evaluate
//      the antecedent at consteval before passing it to `implies`;
//      this fixture proves the predicate handles a non-trivial
//      computed boolean correctly.
//   2. The companion fixture `(true, false)` uses literal bools.
//      A buggy impl `return antecedent_was_a_bool_literal_T &&
//      consequent_was_a_bool_literal_F ? false : true;` (a wholly
//      pathological special-case) would pass the companion but
//      fail this fixture.  The orthogonality is real — albeit
//      against a more pathological bug class than companion #1.
//   3. This fixture also serves as a documentation witness: the
//      production-cite shape is `pre
//      (decide::implies(decide::SOMETHING(...), other_clause))`,
//      and seeing the fixture fail confirms that the cite shape
//      compiles cleanly when the antecedent computes true and
//      the consequent fails.
//
// Anti-pattern targeted: pure-literal evaluators of implication
// that fail to handle compositional/dynamic antecedents.
// Specific shapes:
//
//   constexpr bool implies(bool a, bool c) {
//     // Hypothetical buggy impl that only matches literal bool
//     // arguments at compile time.  Does NOT exist in C++ (we
//     // can't pattern-match on literal-vs-computed at consteval),
//     // but the fixture serves as a regression witness against
//     // any future "optimization" that accidentally short-
//     // circuits when the antecedent is statically known.
//     ...
//   }
//
// The more practical anti-pattern catch: this fixture doubles as
// the "documentation by example" of the canonical production-cite
// shape.  A code reviewer reading this fixture immediately
// understands the production usage pattern.
//
// Distinct from the companion fixture (direct-literal):
//   * direct (companion)               — `implies(true, false)`.
//     The simplest possible witness against always-true /
//     wrong-direction bugs.
//   * computed-antecedent (this)       — antecedent from
//     `decide::is_power_of_two_le(8u, 64u)`, consequent literal
//     `false`.  Pins compositional VC discharge.
//
// Both fixtures fail with the same diagnostic family — what
// differs is the bug class they pin and the documentation shape
// they preserve.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr bool gate(bool antecedent, bool consequent) noexcept {
    CRUCIBLE_PRE(crucible::decide::implies(antecedent, consequent));
    return true;
}

// Antecedent computed from another decide::* predicate so the
// fixture pins the "compositional VC discharge" use shape rather
// than literal-bool plug-in.  is_power_of_two_le(8, 64) returns
// true (8 is 2³, less than or equal to 64); consequent is false.
// `implies(true, false) = false`; CRUCIBLE_PRE traps at consteval.
constexpr auto witness = gate(
    crucible::decide::is_power_of_two_le<std::uint64_t>(8, 64),
    false);

}  // namespace

int main() { return 0; }
