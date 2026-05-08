// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// conjunction, mismatch class #1: SINGLE-ELEMENT FALSE.
//
// Pins the minimal-size negative case: a one-element span carrying
// `false`.  The predicate returns false; CRUCIBLE_PRE fires
// `__builtin_trap()` at consteval, which the front-end rejects as
// "non-constant condition".
//
// Witness `xs = {false}`.  This is the smallest possible witness
// against an "always-true" buggy implementation.  Any
// implementation that returns true unconditionally would COMPILE
// this fixture, while the correct implementation rejects it.
//
// In production this bug manifests as: a multi-clause precondition
// composed via `decide::conjunction(clauses)` admits a single
// false clause as if it were vacuously true.  The kernel runs
// despite a violated invariant; the bug surfaces downstream as
// undefined behavior at the use site (memory corruption, wrong
// hashes, etc.) rather than at the precondition check.
//
// The bug is sneaky because:
//
//   1. Every multi-clause `pre (A && B && C)` site, when migrated
//      to `pre (decide::conjunction({A, B, C}))`, produces
//      identical behavior on all-positive inputs.  The buggy
//      "always-true" only manifests when at least one clause is
//      false — which by construction is the case the precondition
//      is supposed to catch.
//   2. A buggy `disjunction-instead-of-conjunction` impl returns
//      `false` on this single-`false` input (correctly), so this
//      fixture would NOT catch that bug.  The COMPANION fixture
//      (last-element-false multi-clause) catches it.
//
// Anti-pattern targeted: silent identity-fold conjunction
// implementations (`return true;` no matter the input).  This is
// the most basic regression to guard against.
//
// Distinct from the companion fixture (last-false multi-element):
//   * single-false (this fixture)  — `{false}`. Pins the
//     "always-true" / no-op-impl bug class.
//   * last-false (companion)       — `{true, true, true, false}`.
//     Pins the "OR-instead-of-AND" bug class — a buggy impl that
//     short-circuits on the FIRST `true` would WRONGLY accept
//     this input (sees `true`, returns `true`).
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for conjunction: a no-op impl passes the OR-bug fixture (returns
// `true`, conjunction trap, fixture compiles fail under correct
// impl, fixture compiles ok under no-op impl), and the OR-bug
// passes the single-false fixture (returns `false`, conjunction
// trap, fixture compiles fail).
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

// `xs = {false}` — single-element false span.  Conjunction returns
// false; CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches
// any "always-true" buggy implementation.
constexpr bool kFixtureXs[] = {false};
constexpr auto witness = gate(std::span<const bool>{kFixtureXs});

}  // namespace

int main() { return 0; }
