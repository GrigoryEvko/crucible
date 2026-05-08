// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// weakly_increasing, mismatch class #1: MIDDLE-PAIR REGRESSION.
//
// Pins that the predicate correctly rejects a sequence containing a
// strict regression at an INTERIOR pair when the endpoints (front
// and back) are themselves weakly ordered.  Equal pairs would PASS
// weakly_increasing (that is precisely the strict-vs-weak distinction
// vs. CONTRACT-041); so this fixture plants a STRICT regression
// (xs[i-1] > xs[i]) — the ONLY bug class weakly_increasing rejects.
//
// Anti-pattern targeted: `pre(xs.front() <= xs.back())` endpoint
// shortcut.  Endpoints {0, 7} satisfy `front <= back`, but the
// interior pair (5, 3) is a strict regression that endpoint-only
// checks silently accept.  Production data with this shape is
// catastrophic for offset chains (TraceGraph CSR row-pointer arrays,
// StorageSlot offset chains, generation counters where stalling is
// admissible but reversal is corruption).
//
// Distinct from the companion fixture (tail_regress):
//   * middle_regress (this fixture) — fires on `{0, 5, 3, 7}`.
//     Endpoints in correct order; INTERIOR pair regresses.  Catches
//     the front<back endpoint-shortcut anti-pattern.
//   * tail_regress  — fires on `{1, 2, 3, 5, 4}`.
//     All but the final pair are weakly increasing; FINAL pair
//     regresses.  Catches the "first-K-pairs only" / partial-scan
//     anti-pattern.
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

// Middle regression: index 1->2 has 5 -> 3 (strict backward).
// Endpoints (0, 7) satisfy `front <= back`; the front-back shortcut
// accepts.  weakly_increasing walks every consecutive pair and
// rejects on the (5 > 3) branch.  CRUCIBLE_PRE's __builtin_trap
// fires at consteval.
constexpr uint32_t regression_offsets[] = {0u, 5u, 3u, 7u};
constexpr auto witness = gate(regression_offsets);

}  // namespace

int main() { return 0; }
