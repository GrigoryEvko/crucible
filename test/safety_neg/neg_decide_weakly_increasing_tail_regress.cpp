// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// weakly_increasing, mismatch class #2: TAIL-PAIR REGRESSION.
//
// Pins that the predicate correctly rejects a sequence whose first
// (n-1) consecutive pairs are weakly increasing but whose FINAL
// pair regresses.  Targets a different anti-pattern from the
// middle-regression companion fixture: "scan only the first K
// pairs" / partial-scan / early-exit-on-first-good-prefix.  A
// hand-rolled loop that bails out after observing K weakly-ordered
// pairs (K < n) misses any regression after position K.
//
// The tail-regression bug class is real: production code that
// builds a sequence by appending one element at a time and calls
// the predicate at each append is paying O(n²) work and (worse)
// silently accepts a sequence whose final append violates the
// invariant — because the inductive precondition ("the previous
// (n-1) elements were weakly increasing") admits the exact pattern
// this fixture plants.  The fix is to call the predicate on the
// COMPLETE sequence, not on each prefix.
//
// Distinct from the companion fixture (middle_regress):
//   * middle_regress — fires on `{0, 5, 3, 7}`.  Endpoints OK;
//     INTERIOR pair regresses.  Endpoint-shortcut anti-pattern.
//   * tail_regress (this fixture) — fires on `{1, 2, 3, 5, 4}`.
//     Pairs (1,2), (2,3), (3,5) all pass; FINAL pair (5,4) is
//     a strict regression.  First-K-pairs / partial-scan anti-
//     pattern.  An "every prefix is weakly increasing therefore
//     the whole sequence is" induction is WRONG: the induction
//     step requires checking the new tail pair, which is exactly
//     what this fixture plants as broken.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr bool gate(std::span<const uint32_t> offs) noexcept {
    CRUCIBLE_PRE(crucible::decide::weakly_increasing(offs));
    return true;
}

// Tail regression: pairs (1,2), (2,3), (3,5) all weakly increasing;
// the FINAL pair (5, 4) is a strict backward regression.  A
// partial-scan that stops after K=3 successful pairs would silently
// accept this sequence.  weakly_increasing's full-pair scan rejects
// on the (5 > 4) branch.  CRUCIBLE_PRE's __builtin_trap fires at
// consteval.
constexpr uint32_t tail_regression_offsets[] = {1u, 2u, 3u, 5u, 4u};
constexpr auto witness = gate(tail_regression_offsets);

}  // namespace

int main() { return 0; }
