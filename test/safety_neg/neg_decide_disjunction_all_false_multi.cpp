// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// disjunction, mismatch class #2: ALL-FALSE MULTI-ELEMENT (always-
// true bug class).
//
// Pins the multi-element negative case: a four-element span of
// all-false values.  The predicate returns false (none of the
// clauses hold); CRUCIBLE_PRE fires `__builtin_trap()` at
// consteval, which the front-end rejects as "non-constant
// condition".
//
// Witness `xs = {false, false, false, false}`.  This is the
// smallest non-trivial witness against a buggy "always-true"
// disjunction implementation: a no-op impl that returns `true`
// regardless of input would COMPILE this fixture, while the
// correct impl rejects it.
//
// In production this bug manifests as: an "at-least-one-of"
// precondition composed via `decide::disjunction(options)` admits
// a totally-failed option list as if some option held.  The
// kernel runs despite NO option being valid; the bug surfaces
// downstream as a use-of-undefined-default at the use site
// (memory corruption, wrong hashes, NaN propagation, etc.).
//
// Concretely: a runtime hardware-feature check
// `decide::disjunction({has_avx512, has_avx2, has_sse4_2})`
// admits a CPU with NONE of those features as if AVX2 were
// available; subsequent dispatch to AVX2 kernel results in
// SIGILL.  Or worse: a fallback path is bypassed and silent
// wrong-answer mode kicks in.
//
// The bug is sneaky because:
//
//   1. A buggy "always-true" disjunction is a strict superset of
//      a buggy "always-true" conjunction: it accepts everything
//      conjunction would accept PLUS everything conjunction
//      would reject.  This bug is even more permissive than the
//      conjunction analog and equally invisible to all-positive
//      test inputs.
//   2. The companion fixture (empty-span `{}`) does NOT catch
//      this bug uniformly: a no-op impl returning `true` accepts
//      the empty fixture too, so BOTH fixtures fail compilation
//      (correctly rejecting the no-op impl).  Good — catches the
//      no-op impl twice.
//   3. A subtle bug like "return xs[0]" would fail this fixture
//      (xs[0] = false, rejects) and would fail an all-true
//      companion (xs[0] = true, accepts).  Pinned.
//   4. The fixture has 4 elements, not 2, to also stress the
//      fold's iteration logic — a buggy `return xs[0] || xs[1]`
//      that drops the tail would still reject all-false input
//      (correctly!) but a bug that uses the WRONG index
//      (`return xs[xs.size()]` = OOB read of vacuous-true) would
//      manifest only with multiple elements.  Defense in depth.
//
// Anti-pattern targeted: silent identity-fold disjunction
// implementations (`return true;` no matter the input).  This is
// the most basic regression to guard against.  Specific shapes:
//
//   return true;
//     // CONSTANT-TRUE — defeats the disjunction entirely.
//
//   return !xs.empty();
//     // NON-EMPTY-AS-TRUE — accepts any non-empty input
//     // regardless of element values.  This fixture catches it
//     // (4-element non-empty span returns true wrongly).
//
//   bool out = false;
//   for (bool b : xs) out |= true;  // typo / copy-paste bug
//   return out;
//     // ACCUMULATOR-IGNORES-ELEMENT — the loop body uses a
//     // literal instead of the element.  Equivalent to "any
//     // non-empty span returns true."  This fixture catches it.
//
// Distinct from the companion fixture (empty-span):
//   * empty-span (companion)       — `{}`. Pins vacuous-truth
//     identity bug.  Catches WRONG-IDENTITY-style buggy impls.
//   * all-false (this fixture)     — `{false, false, false, false}`.
//     Pins always-true bug class.  Catches CONSTANT-TRUE-style
//     and NON-EMPTY-AS-TRUE-style buggy impls.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for disjunction:
//   - WRONG-IDENTITY (∨ ∅ = ⊤): empty-span catches, all-false
//     companion does not (companion's fold path correctly
//     produces false).
//   - CONSTANT-TRUE / NON-EMPTY-AS-TRUE: this fixture catches,
//     empty-span companion may not (constant-true also fails
//     empty-span — caught twice, redundancy is fine; non-empty-
//     as-true does NOT trigger on empty input — only this fixture
//     catches it).
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

// `xs = {false, false, false, false}` — four-element all-false
// span.  Disjunction returns false (no clause holds);
// CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches any
// "always-true" / "non-empty-is-true" buggy implementation.
constexpr bool kFixtureXs[] = {false, false, false, false};
constexpr auto witness = gate(std::span<const bool>{kFixtureXs});

}  // namespace

int main() { return 0; }
