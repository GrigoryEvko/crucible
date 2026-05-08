// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// no_overflow_pow2_shift, mismatch class #1: SHIFT COUNT OUT OF RANGE.
//
// Pins that the predicate correctly fires inside CRUCIBLE_PRE at
// consteval when called with a shift count `b >= bitwidth(T)` —
// the [expr.shift] UB class that the cleanest manual checks
// (`pre (b < 64)`, `pre (b < sizeof(T)*8)`) get wrong for the
// boundary case OR for cross-width T.
//
// Distinct from the companion fixture (signed-negative-shift):
// this trap fires the predicate's first short-circuit branch
// (`b >= W`), the companion fires the second (`a < 0` for signed T).
// Both UB classes are operator-level (i.e., `a << b` itself is UB,
// not just out-of-range arithmetic), and both must be detected
// BEFORE evaluating any shift expression.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr uint32_t shl_u32(uint32_t a, uint32_t b) noexcept {
    CRUCIBLE_PRE(crucible::decide::no_overflow_pow2_shift(a, b));
    return a << b;
}

// b == 32 == bitwidth(uint32_t): UB at the operator per [expr.shift]/1.
// no_overflow_pow2_shift returns false on the `b >= W` branch;
// CRUCIBLE_PRE's __builtin_trap() fires at consteval; surrounding
// static_assert reports "non-constant condition".
constexpr auto witness = shl_u32(uint32_t{1}, uint32_t{32});

}  // namespace

int main() { return 0; }
