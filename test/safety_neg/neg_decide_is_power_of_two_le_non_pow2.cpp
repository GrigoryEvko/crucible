// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// is_power_of_two_le, mismatch class #1: NON-POWER-OF-TWO BELOW BOUND.
//
// Pins that the predicate correctly rejects a value that satisfies
// the bound conjunct (x <= bound) but NOT the power-of-two conjunct.
// The witness `48` is `32 + 16` — even, divisible by 4, smaller
// than the bound — but has TWO bits set in its binary representation.
//
// Anti-pattern targeted: `pre(x % 2 == 0)` / `pre((x & 1) == 0)`
// even-number test misunderstood as "power of two".  This is the
// most common form of the bug in production: a hash-table capacity
// check that uses "even number ≤ N" instead of "power of two ≤ N",
// then computes `mask = capacity - 1` and indexes into the bucket
// array.  For capacity = 48, the mask becomes 47 (binary 101111)
// — which doesn't isolate any group of bits cleanly, and the
// rehash/probe distribution becomes non-uniform.  Subtle latency
// regressions, no crash, no UB — just slow.
//
// 48 is chosen specifically because:
//
//   * 48 % 2 == 0   (the `% 2 == 0` anti-pattern accepts)
//   * 48 % 4 == 0   (the `% 4 == 0` anti-pattern accepts)
//   * 48 <= 64       (the bound check accepts)
//   * popcount(48) == 2  (the power-of-two test rejects)
//
// Distinct from the companion fixture (above_bound):
//   * non_pow2 (this fixture)  — fires on 48 with bound 64.
//     Value within bound but bit pattern has multiple set bits.
//     Catches the modulo / bit-mask anti-patterns.
//   * above_bound              — fires on 128 with bound 64.
//     Value IS a power of two (1 bit set) but exceeds bound.
//     Catches the popcount-only / has_single_bit anti-patterns
//     that omit the capacity ceiling.
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

// 48 = 32 + 16 (binary 110000) — TWO bits set, not a power of two.
// 48 < 64 (within bound), 48 is even (divisible by 2 AND 4 AND 8 AND
// 16), but is_power_of_two_le's (x & (x - 1)) test rejects it
// because (48 & 47) == 32, not zero.  CRUCIBLE_PRE's __builtin_trap
// fires at consteval.
constexpr auto witness = gate(48);

}  // namespace

int main() { return 0; }
