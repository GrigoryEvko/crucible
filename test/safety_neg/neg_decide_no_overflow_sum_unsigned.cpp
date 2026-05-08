// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// no_overflow_sum over UNSIGNED integer types.  Pins that the
// predicate correctly fails-to-compile when wrapped in CRUCIBLE_PRE
// and called at consteval with operands whose sum exceeds the
// maximum representable value of T.
//
// Distinct mismatch class: UNSIGNED wrap-around (UINT_MAX + 1 → 0).
// Companion fixture neg_decide_no_overflow_sum_signed.cpp covers the
// SIGNED asymmetric path (negative-side underflow at INT_MIN), a
// distinct branch in __builtin_add_overflow's detection logic.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <limits>

namespace {

[[nodiscard]] constexpr uint64_t add_u64(uint64_t a, uint64_t b) noexcept {
    CRUCIBLE_PRE(crucible::decide::no_overflow_sum(a, b));
    return a + b;
}

// UINT64_MAX + 1: mathematical sum is 2^64, NOT representable in
// uint64_t.  __builtin_add_overflow returns true; no_overflow_sum
// returns false; CRUCIBLE_PRE's __builtin_trap() fires at consteval;
// the surrounding static_assert reports "non-constant condition".
constexpr auto witness =
    add_u64(std::numeric_limits<uint64_t>::max(), uint64_t{1});

}  // namespace

int main() { return 0; }
