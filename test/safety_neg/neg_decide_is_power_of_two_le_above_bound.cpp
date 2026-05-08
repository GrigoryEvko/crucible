// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// is_power_of_two_le, mismatch class #2: POWER-OF-TWO ABOVE BOUND.
//
// Pins that the predicate correctly rejects a value that satisfies
// the power-of-two conjunct (single bit set) but EXCEEDS the bound.
// The witness `128` is exactly `2^7` — clean power of two, popcount
// is 1, but 128 > 64 (the SwissCtrl group-width ceiling).
//
// Anti-pattern targeted: `pre(__builtin_popcount(x) == 1)` /
// `pre(std::has_single_bit(x))` popcount-only test.  A reviewer
// who reads "x must be a power of two" and writes the C++20
// `std::has_single_bit` correctly catches the {3, 5, 6, 12} class
// — but FORGETS the capacity ceiling.  Production code that uses
// `1 << bit` to compute a mask then writes 128 bytes of state
// into a 64-byte allocation: stack corruption, silent.
//
// 128 is chosen specifically because:
//
//   * popcount(128) == 1     (the popcount-only anti-pattern accepts)
//   * 128 == (1 << 7)         (the 1-bit-shift anti-pattern accepts)
//   * has_single_bit(128)     (C++20 idiom accepts)
//   * 128 > 64               (the bound check rejects)
//
// Distinct from the companion fixture (non_pow2):
//   * non_pow2          — fires on 48 with bound 64.
//     Within bound but multiple set bits (catches modulo / bit-mask
//     anti-patterns).
//   * above_bound (this fixture) — fires on 128 with bound 64.
//     Power of two but above bound (catches popcount-only /
//     has_single_bit / shift-only anti-patterns).
//
// The two fixtures together pin BOTH conjuncts of the predicate:
// a reviewer who satisfies one MUST satisfy the other.  A cite
// of is_power_of_two_le is the single discharge that protects
// both bug classes.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstddef>

namespace {

[[nodiscard]] constexpr std::size_t gate(std::size_t w) noexcept {
    CRUCIBLE_PRE(crucible::decide::is_power_of_two_le<std::size_t>(w, 64));
    return w;
}

// 128 = 2^7 — perfectly a power of two (popcount == 1), but exceeds
// the bound 64 by one binary order.  is_power_of_two_le's `x > bound`
// branch rejects.  CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr auto witness = gate(128);

}  // namespace

int main() { return 0; }
