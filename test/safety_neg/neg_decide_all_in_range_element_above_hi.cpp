// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// all_in_range, mismatch class #2: ELEMENT ABOVE UPPER BOUND.
//
// Pins that the predicate correctly fires inside CRUCIBLE_PRE at
// consteval when called with a span containing an element strictly
// greater than `hi` — the `x > hi` branch of the per-element check.
//
// Distinct from the companion fixture (element-below-lo):
//   * element_above_hi  — fires the `x > hi` short-circuit, exercises
//     the upper-bound branch.  Catches the production bug class
//     where a counter that should be capped at MAX_OPS=146 (CKernel
//     taxonomy) drifts past the cap in a deserialized region — a
//     bug class that endpoint-only "ids[0] >= 0 && ids[N-1] < MAX"
//     manual checks would silently miss when the violator is at
//     index 1 of a 3-element span.
//   * element_below_lo  — fires the `x < lo` short-circuit, distinct
//     branch in the same procedure.
//
// This fixture also pins the same "middle violator" anti-pattern
// catch as its companion: the violating element is at index 1 (not
// the endpoints), so endpoint-only manual checks would silently pass.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr bool gate(std::span<const uint32_t> ids,
                                  uint32_t lo,
                                  uint32_t hi) noexcept {
    CRUCIBLE_PRE(crucible::decide::all_in_range(ids, lo, hi));
    return true;
}

// Middle element (200) is above hi (146 — CKernel NUM_KERNELS cap).
// Endpoint-only manual checks would pass: ids[0] = 10 ≤ 146 and
// ids[2] = 50 ≤ 146.  all_in_range walks every element and rejects
// index 1 on the `x > hi` branch.  CRUCIBLE_PRE's __builtin_trap
// fires at consteval.
constexpr uint32_t ids[] = {10u, 200u, 50u};
constexpr auto witness = gate(ids, 0u, 146u);

}  // namespace

int main() { return 0; }
