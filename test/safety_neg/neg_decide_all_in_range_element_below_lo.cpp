// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// all_in_range, mismatch class #1: ELEMENT BELOW LOWER BOUND.
//
// Pins that the predicate correctly fires inside CRUCIBLE_PRE at
// consteval when called with a span containing an element strictly
// less than `lo` — the `x < lo` branch of the per-element check.
//
// Distinct from the companion fixture (element-above-hi):
//   * element_below_lo  — fires the `x < lo` short-circuit, exercises
//     signed-comparison (a negative element vs non-negative lo bound).
//     Catches the production bug class where an unsigned-typed slot
//     ID is reinterpreted as signed and a -1 sentinel slips through.
//   * element_above_hi  — fires the `x > hi` short-circuit, exercises
//     the upper-bound branch.  Different bug class because most
//     production bounds checks (`if (idx < n)`) catch the upper
//     side cleanly but miss the lower side under signed/unsigned
//     conversion drift.
//
// This fixture also pins the "middle violator" anti-pattern catch:
// the violating element is at index 1 (not the endpoints), so
// endpoint-only manual checks would silently pass.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr bool gate(std::span<const int32_t> ids,
                                  int32_t lo,
                                  int32_t hi) noexcept {
    CRUCIBLE_PRE(crucible::decide::all_in_range(ids, lo, hi));
    return true;
}

// Middle element (-1) is below lo (0).  Endpoint-only manual checks
// would pass: ids[0] = 10 ≥ 0 and ids[2] = 90 ≤ 100.
// all_in_range walks every element and rejects index 1 on the
// `x < lo` branch.  CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr int32_t ids[] = {10, -1, 90};
constexpr auto witness = gate(ids, 0, 100);

}  // namespace

int main() { return 0; }
