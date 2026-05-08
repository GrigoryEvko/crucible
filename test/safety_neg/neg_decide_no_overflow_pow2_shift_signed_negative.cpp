// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// no_overflow_pow2_shift, mismatch class #2: LEFT-SHIFT OF SIGNED
// NEGATIVE VALUE.
//
// Pins that the predicate correctly fires inside CRUCIBLE_PRE at
// consteval when called with `a < 0` on a signed T — the [expr.shift]
// UB class that no shift-count check can catch and that
// `pre (b < bitwidth)` will silently miss.
//
// Distinct from the companion fixture (shift-count-out-of-range):
// this trap fires the predicate's THIRD branch (`a < 0` for signed T)
// after both range checks pass.  The bug class is "negative-base
// shift": even with a perfectly valid shift count, `(-1) << 1` is
// UB at the operator per [expr.shift]/2 (post-C++14: also on signed
// overflow into sign bit; this fixture uses the simpler -1 form).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr int32_t shl_i32(int32_t a, int32_t b) noexcept {
    CRUCIBLE_PRE(crucible::decide::no_overflow_pow2_shift(a, b));
    return a << b;
}

// a == -1, b == 1: shift count is in range, but `(-1) << 1` is UB
// per [expr.shift]/2 (left-shift of signed negative).
// no_overflow_pow2_shift returns false on the `a < 0` branch;
// CRUCIBLE_PRE's __builtin_trap() fires at consteval; surrounding
// static_assert reports "non-constant condition".
constexpr auto witness = shl_i32(int32_t{-1}, int32_t{1});

}  // namespace

int main() { return 0; }
