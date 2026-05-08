// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// strictly_increasing, mismatch class #2: MIDDLE-PAIR REGRESSION.
//
// Pins that the predicate correctly rejects a sequence with a
// genuine `x[i-1] > x[i]` regression in the middle.  This is the
// bug class where a counter goes BACKWARD — a different failure
// mode from the equal-pair class:
//
//   * Equal-pair: counter stalls (x[i-1] == x[i]).  Distinguished
//     class for strict-vs-weak.  Companion fixture covers it.
//   * Middle-regression (this fixture): counter goes backward
//     (x[i-1] > x[i]).  Caught by both strict AND weak ordering
//     predicates, but the consequence in production is severe:
//     event-sourced replay sees an out-of-order record, breaks
//     causal ordering, corrupts state.
//
// The regression is placed at the middle pair (index 1->2) of a
// 4-element span to pin the "endpoint-only manual check" anti-
// pattern: pre(front() < back()) would silently pass {1, 9, 5, 10}
// because 1 < 10.  pre(xs[0] < xs[n-1]) misses ANY interior
// regression by construction.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr bool gate(std::span<const int64_t> ids) noexcept {
    CRUCIBLE_PRE(crucible::decide::strictly_increasing(ids));
    return true;
}

// Middle regression: index 1->2 has 9 -> 5 (backward).  Endpoints
// (1, 10) satisfy `front < back`; the front-back shortcut accepts.
// strictly_increasing walks every consecutive pair and rejects on
// the !(9 < 5) branch.  CRUCIBLE_PRE's __builtin_trap fires at
// consteval.
constexpr int64_t regression_seq[] = {1, 9, 5, 10};
constexpr auto witness = gate(regression_seq);

}  // namespace

int main() { return 0; }
