// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// non_negative, mismatch class #2: VALUE-NEGATIVE-BOUNDARY-MAGNITUDE
// (abs-based-confusion / width-narrowing-truncation / negation-
// overflow violator).
//
// Pins the non-strict-positive lower-bound clause `T{0} <= x`
// against a SIGNED witness at the negation-overflow boundary.
// Witness: `non_negative(int32_t{INT32_MIN})`.  CRUCIBLE_PRE fires
// `__builtin_trap()` at consteval because non_negative(INT_MIN)
// correctly returns false; the front-end rejects with "non-constant
// condition".
//
// This fixture exists to catch boundary-magnitude bug classes that
// the companion (minus_one) fixture CANNOT detect.  The bug
// shapes:
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) { return std::abs(x) == x; }
//     // ABS-BASED-CONFUSION at boundary.
//     // For T=int32_t, x=-1: std::abs(-1) = 1, 1 == -1 is FALSE,
//     //   correctly rejects -1.  minus_one fixture does NOT catch.
//     // For T=int32_t, x=INT_MIN: std::abs(INT_MIN) is undefined
//     //   behavior in signed arithmetic (typically returns INT_MIN
//     //   due to two's-complement negation overflow), so == x is
//     //   TRUE → admits INT_MIN.  THIS FIXTURE CATCHES.
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) {
//       return static_cast<std::uint8_t>(x) <= INT8_MAX;
//   }
//     // WIDTH-NARROWING-TRUNCATION at boundary.
//     // For T=int32_t, x=-1: uint8_t(-1) = 0xFF = 255, 255 > 127,
//     //   correctly rejects -1.  minus_one fixture does NOT catch.
//     // For T=int32_t, x=INT_MIN: uint8_t(INT_MIN) = 0 (truncates
//     //   to low byte, which IS 0 for INT_MIN = 0x80000000 →
//     //   low byte = 0x00).  0 <= 127, admits INT_MIN.  THIS
//     //   FIXTURE CATCHES.
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) { return -(-x) >= 0; }
//     // NEGATION-OVERFLOW.
//     // For T=int32_t, x=-1: -(-(-1)) = -1, -1 >= 0 = false,
//     //   correctly rejects.  minus_one fixture does NOT catch.
//     // For T=int32_t, x=INT_MIN: -INT_MIN is UB (signed overflow),
//     //   typically yields INT_MIN, so -(-INT_MIN) = -INT_MIN =
//     //   undefined → INT_MIN, which is < 0, correctly rejects.
//     //   Hmm, depends on optimizer; under -ftrapv this aborts,
//     //   under -fwrapv this wraps.  This particular bug is
//     //   compiler-dependent rather than always over-admitting.
//     //   Listed for completeness; not the primary catch.
//
// In Crucible production code, the abs-based confusion bug class
// is the most insidious of the boundary-magnitude family: a
// well-meaning developer who reads `non_negative` as "the absolute
// value equals the original" implements a predicate that LOOKS
// correct on every value EXCEPT the saturated minimum.  The bug
// fires at the precise boundary where INT_MIN hides in counter
// fields:
//
//   - Topology::select_warm_cpus(count): production code rarely
//     passes INT_MIN as `count`, but the predicate STILL needs to
//     reject it.  A buggy abs-based impl would silently accept
//     INT_MIN and overflow `same_numa.reserve(static_cast<size_t>
//     (INT_MIN))`.  size_t(INT_MIN) on x86_64 = 0xFFFFFFFF80000000,
//     which is ~2^63 bytes — allocator returns SIGSEGV.
//
//   - Monotonic::bump_by(delta): a programmer who accidentally
//     passes `INT_MIN` (e.g., from a underflowed subtraction
//     `low - high` where low > high) would expect the contract
//     to reject.  A buggy abs-based impl admits, and bump_by then
//     calls `value_.fetch_add(INT_MIN)` — which wraps the counter
//     to INT_MAX-side and breaks the wrapper's monotonicity
//     invariant catastrophically.
//
// The bug is sneaky in code review because:
//
//   1. The `std::abs` cite reads as "this returns the magnitude;
//      if the magnitude equals the original, the original was
//      non-negative".  That READS correct.  But the boundary
//      INT_MIN where -INT_MIN overflows is exactly the case
//      where the predicate's intent and the implementation
//      DIVERGE.
//
//   2. Unit tests on small-magnitude negatives PASS — abs(-1)=1,
//      abs(-42)=42, all reject correctly.  The bug only manifests
//      at INT_MIN, which thin test coverage typically omits.
//
//   3. The bug class is structural, not a typo: it's the
//      semantically-wrong predicate, not a misspelling.  Linters
//      and clang-tidy cannot flag it.  Even property-based fuzzers
//      would need to specifically sample boundary values to catch.
//
// Distinct from the companion fixture (non_negative_minus_one):
//   * minus_one (companion)      — witness -1.  Catches ALWAYS-
//     ACCEPT / INVERTED-SENSE / STRICT-INVERSE / WRONG-CITE-TO-
//     IS-NON-ZERO.  Small-magnitude negative.  Cannot detect
//     abs-based confusion (abs(-1)=1, !=-1, correct rejection)
//     or width-narrowing truncation (uint8_t(-1)=255, > 127,
//     correct rejection).
//   * int_min (this fixture)     — witness INT32_MIN.  Catches
//     boundary-magnitude-only bugs: abs-based confusion (admits
//     INT_MIN due to negation overflow), width-narrowing
//     truncation (admits INT_MIN due to low-byte zero).  The
//     ALWAYS-ACCEPT and STRICT-INVERSE bugs are ALSO caught here
//     (defense in depth), but the unique catch is at the boundary.
//
// Together the two fixtures span both small-magnitude and boundary-
// magnitude over-admission classes.  This is the minimum HS14 needs.
//
// Anti-pattern targeted: abs-based confusion / width-narrowing
// truncation / negation overflow at the INT_MIN boundary.  Specific
// shapes:
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) { return std::abs(x) == x; }
//     // ABS-BASED — caught at INT_MIN.
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) {
//       return static_cast<std::uint8_t>(x) <= 0x7F;
//   }
//     // WIDTH-NARROWING — caught at INT_MIN (low byte = 0).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <limits>

namespace {

template <typename T>
[[nodiscard]] constexpr bool gate(T x) noexcept {
    CRUCIBLE_PRE(crucible::decide::non_negative(x));
    return true;
}

// INT_MIN is the boundary-magnitude signed witness that distinguishes
// abs-based confusion / width-narrowing truncation from the small-
// magnitude bugs caught by the companion (minus_one) fixture.
// non_negative(INT_MIN) correctly returns false (INT_MIN is below
// zero); CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches
// the boundary bug classes — the predicate-confusion shape that the
// minus_one fixture cannot detect because abs(-1)=1 and uint8_t(-1)
// =255 both correctly land in the rejection set.
constexpr auto witness = gate(std::numeric_limits<std::int32_t>::min());

}  // namespace

int main() { return 0; }
