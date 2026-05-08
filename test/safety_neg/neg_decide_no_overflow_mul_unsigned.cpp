// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// no_overflow_mul over UNSIGNED integer types.  Pins that the
// predicate correctly fails-to-compile when wrapped in CRUCIBLE_PRE
// and called at consteval with overflowing unsigned operands.
//
// Distinct mismatch class: UNSIGNED overflow (saturates upward,
// __builtin_mul_overflow returns true on wrap-around).  Companion
// fixture neg_decide_no_overflow_mul_signed.cpp covers the SIGNED
// path, which involves a different overflow detection branch in
// __builtin_mul_overflow.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".
// The exact wording depends on which compiler pass evaluates the
// predicate first; all four spellings are valid GCC 16.1.1 diagnostic
// outputs from CRUCIBLE_PRE's if-consteval-trap pattern.

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <limits>

namespace {

[[nodiscard]] constexpr uint64_t mul_u64(uint64_t a, uint64_t b) noexcept {
    CRUCIBLE_PRE(crucible::decide::no_overflow_mul(a, b));
    return a * b;
}

// Force consteval evaluation with operands that overflow uint64_t.
// UINT64_MAX * 2 saturates to 0xFFFF'FFFF'FFFF'FFFE * 2 wraps; the
// __builtin_mul_overflow flag fires, no_overflow_mul returns false,
// CRUCIBLE_PRE's __builtin_trap() is invoked at consteval, and the
// surrounding static_assert reports "non-constant condition".
constexpr auto witness =
    mul_u64(std::numeric_limits<uint64_t>::max(), uint64_t{2});

}  // namespace

int main() { return 0; }
