// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// conjunction, mismatch class #2: LAST-ELEMENT FALSE in a multi-
// element span (OR-instead-of-AND bug class).
//
// Pins the multi-element negative case: a four-element span whose
// LAST element is false but earlier elements are true.  The
// predicate returns false; CRUCIBLE_PRE fires `__builtin_trap()` at
// consteval, which the front-end rejects as "non-constant
// condition".
//
// Witness `xs = {true, true, true, false}`.  This is the smallest
// witness against an "OR-instead-of-AND" buggy implementation: a
// buggy `any_of` impl would short-circuit on the FIRST `true` and
// WRONGLY accept this input as if the conjunction held.
//
// In production this bug manifests as: a multi-clause precondition
// composed via `decide::conjunction(clauses)` admits a partially-
// satisfied set of clauses as if all clauses held.  The kernel
// runs despite ONE violated invariant; the bug surfaces downstream
// as undefined behavior at the use site.  This bug is dramatically
// worse than the "always-true" no-op bug because it sometimes
// rejects (when ALL clauses are false) and sometimes accepts (when
// ANY clause is true) — the production behavior LOOKS like
// conjunction is being checked, just incorrectly.
//
// The bug is sneaky because:
//
//   1. A buggy `any_of` impl rejects the all-false input correctly
//      and accepts the all-true input correctly.  The mid-cases
//      (1, 2, 3 of N false) are where the divergence appears.
//   2. The companion fixture (single-`{false}`) does NOT catch this
//      bug: a buggy `any_of` returns `false` on `{false}`, the
//      conjunction-trap fires, and the fixture compiles fail under
//      both correct and buggy impls.  This is why we need TWO
//      orthogonal fixtures for conjunction.
//   3. A truly subtle bug (return `xs[0]`, ignore the rest) WOULD
//      pass this fixture (xs[0] = true, accepts).  But it would
//      fail the `{false}` fixture (xs[0] = false, rejects).  Pinned.
//
// Anti-pattern targeted: silent disjunction-fold — a buggy
// implementation that uses `any_of`/OR-fold semantics where AND
// was intended.  Specific shapes:
//
//   for (bool b : xs) if (b) return true;
//   return false;
//     // OR-FOLD — accepts on first true.  Wrong direction entirely.
//
//   return xs.empty() || xs[0];
//     // FIRST-ELEMENT-ONLY — accepts based on xs[0] regardless of
//     // the rest.  This fixture catches it.
//
//   return std::ranges::any_of(xs, std::identity{});
//     // STDLIB OR-FOLD — same as the manual loop above.
//
// Distinct from the companion fixture (single-element-false):
//   * single-false (companion)   — `{false}`. Catches "always-true"
//     / no-op-impl bug class.
//   * last-false (this fixture)  — `{true, true, true, false}`.
//     Catches "OR-instead-of-AND" bug class.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for conjunction: the no-op impl passes the OR-bug fixture (sees
// `true`-prefix, returns `true`, conjunction-trap fires, fixture
// compiles fail under correct impl) and the OR-bug impl passes the
// single-false fixture (sees `false`, returns `false`, conjunction-
// trap fires, fixture compiles fail).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <span>

namespace {

[[nodiscard]] constexpr bool gate(std::span<const bool> xs) noexcept {
    CRUCIBLE_PRE(crucible::decide::conjunction(xs));
    return true;
}

// `xs = {true, true, true, false}` — three trues then a false.
// Conjunction returns false (one clause is false); CRUCIBLE_PRE's
// __builtin_trap fires at consteval.  Catches any "OR-instead-of-
// AND" buggy implementation that short-circuits on first true.
constexpr bool kFixtureXs[] = {true, true, true, false};
constexpr auto witness = gate(std::span<const bool>{kFixtureXs});

}  // namespace

int main() { return 0; }
