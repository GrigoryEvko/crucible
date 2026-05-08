// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// no_overflow_sum over SIGNED integer types.  Pins that the
// predicate correctly fails-to-compile when wrapped in CRUCIBLE_PRE
// and called at consteval with operands that produce a result outside
// the signed range — specifically the NEGATIVE-extreme underflow path.
//
// Distinct mismatch class: SIGNED underflow at INT_MIN.
// `INT64_MIN + (-1)` mathematically equals INT64_MIN - 1, which is
// NOT representable in int64_t — __builtin_add_overflow fires on the
// negative-extreme branch.  This is the asymmetric-range trap that
// catches off-by-one underflow bugs in production code (a bug class
// distinct from unsigned wrap, which the companion fixture covers,
// AND distinct from positive-side INT_MAX+1 overflow because the
// detection branch in __builtin_add_overflow differs).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <limits>

namespace {

[[nodiscard]] constexpr int64_t add_i64(int64_t a, int64_t b) noexcept {
    CRUCIBLE_PRE(crucible::decide::no_overflow_sum(a, b));
    return a + b;
}

// INT64_MIN + (-1): mathematical sum is INT64_MIN - 1, NOT representable
// in int64_t.  This is the canonical signed-underflow asymmetric-range
// trap — it catches a bug class that unsigned overflow checks AND
// positive-side overflow checks both miss.
constexpr auto witness =
    add_i64(std::numeric_limits<int64_t>::min(), int64_t{-1});

}  // namespace

int main() { return 0; }
