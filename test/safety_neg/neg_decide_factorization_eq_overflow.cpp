// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// factorization_eq, mismatch class #2: OVERFLOW DURING RUNNING PRODUCT.
//
// Pins that the predicate correctly rejects a factor list whose
// running product overflows type T mid-scan, EVEN WHEN the wrapped
// value happens to coincide with the expected total.  The witness
// `{65536u, 65536u}` multiplies to 2^32 in arbitrary-precision
// arithmetic; in uint32_t arithmetic the multiplication wraps to 0.
// The expected total is also 0.
//
// A naive implementation that uses bare `*` (e.g. std::accumulate
// with multiplies<>) would compute the wrapped product 0 and
// COMPARE EQUAL to the total 0 — silently accepting a meaningless
// factorization.  factorization_eq's __builtin_mul_overflow check
// catches the carry-out flag and rejects.
//
// In production this manifests as: heterogeneous fleet config
// where world_size = 0 (degenerate / uninitialized) and a hostile
// or untrusted partition descriptor multiplies to a value that
// wraps to 0.  Without overflow detection, the fleet boots with
// "valid" but nonsensical sharding.  With it, the predicate
// rejects at startup.
//
// The witness is deliberately the corner case where overflow
// AND value-equality coincide — proving the predicate's overflow
// detection is independent of the equality check.
//
// Anti-pattern targeted: `pre (std::accumulate(b, e, 1, multiplies<>{})
// == total)` — uses bare `*` which wraps silently on overflow.
// Also pinned: `pre (TP * DP * PP * EP * CP == world_size)` chain
// expressions where a single intermediate exceeds T's range.
//
// Distinct from the companion fixture (wrong_product):
//   * wrong_product — fires on `{8, 2, 4, 1, 2}` vs total 64.
//     Within representable range; mathematical product (128) !=
//     total (64).  Catches "wrong factors written".
//   * overflow (this fixture) — fires on `{65536, 65536}` vs total 0.
//     Mathematical product (2^32) is OUT of uint32_t range; even
//     though the wrapped value (0) equals total (0), the predicate
//     rejects on the overflow-flag branch.  Catches "didn't
//     validate inputs" / "trusted std::accumulate".
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr uint32_t
gate(std::span<const uint32_t> dims, uint32_t world) noexcept {
    CRUCIBLE_PRE(crucible::decide::factorization_eq(dims, world));
    return world;
}

// 65536 * 65536 = 2^32, which OVERFLOWS uint32_t and wraps to 0.
// Caller passes total = 0 to bait a naive implementation that
// would silently accept (since wrapped product equals total).
// __builtin_mul_overflow returns true, factorization_eq returns
// false BEFORE the equality check.  CRUCIBLE_PRE's __builtin_trap
// fires at consteval.
constexpr uint32_t overflow_factors[] = {65536u, 65536u};
constexpr auto witness = gate(overflow_factors, 0u);

}  // namespace

int main() { return 0; }
