// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// disjunction, mismatch class #1: EMPTY SPAN (vacuous-false
// identity).
//
// Pins the empty-input boundary: a zero-element span.  The
// predicate returns false (empty disjunction is vacuously false,
// consistent with `∨ ∅ = ⊥`); CRUCIBLE_PRE fires
// `__builtin_trap()` at consteval, which the front-end rejects
// as "non-constant condition".
//
// Witness `xs = {}` (empty span).  This is the smallest possible
// witness against a buggy "vacuously-true" disjunction
// implementation: a wrong impl that returns `true` for empty input
// (treating it like `∨ ∅ = ⊤`) would COMPILE this fixture, while
// the correct impl rejects it.
//
// In production this bug manifests as: an "at-least-one-of"
// precondition composed via `decide::disjunction(options)` over a
// dynamically-built option list admits an EMPTY option list as
// vacuously satisfied.  The kernel runs with NO options selected;
// the bug surfaces downstream as a use-of-uninitialized-output
// (memory corruption, wrong hashes) because the "selected option"
// is actually nothing.
//
// Concretely: a Forge IR rewrite proposes a set of valid lowering
// rules; if the rule set is EMPTY (no applicable rules), the
// vacuously-true bug accepts and lowering proceeds with no
// substitution; the kernel emits zeros or junk; production
// silently produces wrong outputs.
//
// The bug is sneaky because:
//
//   1. The mathematical convention `∨ ∅ = ⊥` is the ONLY consistent
//      identity for the disjunction monoid (since `false ∨ x = x`,
//      so `false` is the identity).  The `∨ ∅ = ⊤` "convention" is
//      a sign error or a confusion with conjunction's `∧ ∅ = ⊤`.
//      Catching this at consteval prevents the sign error from
//      leaking into production.
//   2. The companion fixture (`{false, false, false, false}`) does
//      NOT catch this bug: a buggy "vacuously-true" impl returns
//      `false` on a non-empty all-false span (the normal fold
//      branch fires, no vacuous case taken), and the fixture
//      correctly rejects.  Both fixtures together catch both bugs.
//   3. A correct impl with the WRONG identity element (initialize
//      accumulator to `true` instead of `false`) would accept this
//      empty-span fixture.  It would also accept the all-false
//      companion fixture (`true || false || false || ... = true`).
//      Either fixture catches that bug, but they catch different
//      manifestations.
//
// Anti-pattern targeted: silent vacuous-truth disjunction.
// Specific shapes:
//
//   bool out = true;
//   for (bool b : xs) out = out || b;
//   return out;
//     // WRONG IDENTITY — initializes accumulator to true.  Empty
//     // span returns true.  This fixture catches it.
//
//   return xs.empty() || std::ranges::any_of(xs, std::identity{});
//     // EMPTY-IS-TRUE GUARD — explicit vacuous-true branch.
//
//   return !xs.empty() ? std::ranges::any_of(xs, std::identity{})
//                      : true;
//     // SAME — explicit vacuous-true branch via ternary.
//
// Distinct from the companion fixture (all-false multi-element):
//   * empty-span (this fixture)    — `{}`. Pins vacuous-truth
//     identity bug.  Catches WRONG-IDENTITY-style buggy impls.
//   * all-false (companion)        — `{false, false, false, false}`.
//     Pins always-true bug class.  Catches CONSTANT-TRUE-style
//     buggy impls (`return true;` no matter the input).
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for disjunction: a vacuous-truth impl passes the all-false
// companion (correct fold, no vacuous case) and fails this empty
// fixture; a constant-true impl passes neither (rejects both at
// consteval).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <span>

namespace {

[[nodiscard]] constexpr bool gate(std::span<const bool> xs) noexcept {
    CRUCIBLE_PRE(crucible::decide::disjunction(xs));
    return true;
}

// `xs = {}` — empty span.  Disjunction returns false (∨ ∅ = ⊥);
// CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches any
// "vacuously-true" buggy implementation.
constexpr auto witness = gate(std::span<const bool>{});

}  // namespace

int main() { return 0; }
