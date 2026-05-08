// SPDX-License-Identifier: Apache-2.0
//
// test/test_decide.cpp
//
// CONTRACT-021 — positive coverage for crucible::decide::* predicate
// library.  Every Decide procedure ships with:
//
//   * Compile-time witnesses — static_asserts proving the predicate
//     correctly accepts safe inputs and rejects unsafe ones.
//   * Runtime witnesses     — exercises the predicate under !NDEBUG
//     to verify the if-consteval branch isn't hiding a regression.
//   * Boundary witnesses    — limits-of-T (zero, max, min for signed)
//     edge cases that fuzz harnesses might miss.
//
// Negative-compile coverage (predicate fires inside CRUCIBLE_PRE at
// consteval) lives in test/safety_neg/neg_decide_*.cpp as separate
// fixtures, per the HS14 mandate adapted for predicate libraries:
// each fixture demonstrates a distinct mismatch class.

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <utility>

namespace {

namespace dc = crucible::decide;

// ── no_overflow_mul positive witnesses ─────────────────────────────
//
// Coverage matrix:
//
//   width × signedness × (zero / one / typical / boundary)
//   ───────────────────────────────────────────────────────────
//   uint8_t       | 0×0=0          | 1×1=1     | 10×20=200    | 16×16=256 (overflow)
//   uint32_t      | 0×x=0          | 1×x=x     | typical      | UINT32_MAX×2 (overflow)
//   uint64_t      | UINT64_MAX×1=ok|           |              | UINT64_MAX×2 (overflow)
//   int32_t       | INT_MIN×-1     | -1×-1=1   | typical      | INT_MAX×2 (overflow)
//   int64_t       | INT64_MIN×-1   |           |              | INT64_MIN×2 (overflow)
//
// The witnesses below cover the "true" cases at consteval; the
// "false" (overflowing) cases are pinned by neg-compile fixtures.

// Trivial cases — multiplying anything by 0 or 1 cannot overflow.
static_assert(dc::no_overflow_mul<uint32_t>(0, 0));
static_assert(dc::no_overflow_mul<uint32_t>(0, std::numeric_limits<uint32_t>::max()));
static_assert(dc::no_overflow_mul<uint32_t>(1, std::numeric_limits<uint32_t>::max()));
static_assert(dc::no_overflow_mul<uint32_t>(std::numeric_limits<uint32_t>::max(), 1));

// Typical safe cases.
static_assert(dc::no_overflow_mul<uint32_t>(10, 20));
static_assert(dc::no_overflow_mul<uint32_t>(0xFFFFu, 0xFFFFu));      // 2^32 - 2^17 + 1, fits

// Boundary cases — exact max representable product.
static_assert(dc::no_overflow_mul<uint8_t>(15, 17));                  // 255 = UINT8_MAX
static_assert(!dc::no_overflow_mul<uint8_t>(16, 16));                 // 256 — overflow
static_assert(!dc::no_overflow_mul<uint32_t>(std::numeric_limits<uint32_t>::max(), 2));

// Signed: positive × positive within range.
static_assert(dc::no_overflow_mul<int32_t>(46340, 46340));            // ~2^31, just fits
static_assert(!dc::no_overflow_mul<int32_t>(46341, 46341));           // overflow
static_assert(!dc::no_overflow_mul<int32_t>(std::numeric_limits<int32_t>::max(), 2));

// Signed: negative × positive — clean detection.
static_assert(dc::no_overflow_mul<int32_t>(-1, std::numeric_limits<int32_t>::max()));
static_assert(!dc::no_overflow_mul<int32_t>(-1, std::numeric_limits<int32_t>::min()));  // -INT_MIN > INT_MAX

// Signed: negative × negative — overflow flips sign.
static_assert(dc::no_overflow_mul<int32_t>(-46340, -46340));
static_assert(!dc::no_overflow_mul<int32_t>(-46341, -46341));

// 64-bit witnesses.
static_assert(dc::no_overflow_mul<uint64_t>(0xFFFF'FFFFu, 0xFFFF'FFFFu));   // safe
static_assert(!dc::no_overflow_mul<uint64_t>(std::numeric_limits<uint64_t>::max(), 2));
static_assert(!dc::no_overflow_mul<int64_t>(std::numeric_limits<int64_t>::min(), -1));  // -INT64_MIN

// ── Composition with CRUCIBLE_PRE — the production usage shape ─────
//
// Verifies the predicate is suitable for consteval-firing inside the
// CRUCIBLE_PRE macro.  The positive case must compile cleanly; the
// negative case (overflow) is pinned by neg_decide_* fixtures.

[[nodiscard]] constexpr uint64_t safe_mul_u64(uint64_t a, uint64_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_mul(a, b));
    return a * b;
}

[[nodiscard]] constexpr int32_t safe_mul_i32(int32_t a, int32_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_mul(a, b));
    return a * b;
}

static_assert(safe_mul_u64(7, 6) == 42);
static_assert(safe_mul_i32(-5, 3) == -15);
static_assert(safe_mul_i32(46340, 46340) == 2'147'395'600);

// ── no_overflow_sum positive witnesses ─────────────────────────────
//
// Coverage matrix (sum is symmetric in operands; we vary
// width × signedness × {zero, one, max-1, exact-max, beyond-max}):
//
//   uint8_t       | 0+0=0  | 1+254=255 (UINT8_MAX) | 255+1 (overflow)
//   uint32_t      | typical safe          | UINT32_MAX+1 (overflow)
//   uint64_t      | UINT64_MAX-1 + 1 = max | UINT64_MAX+1 (overflow)
//   int32_t       | INT32_MAX-1 + 1 = max | INT32_MAX+1 (overflow positive)
//   int32_t       | INT32_MIN+1 + (-1) = min | INT32_MIN+(-1) (overflow negative)
//   int64_t       | INT64_MIN+1 + (-1) = min | INT64_MIN+(-1) (overflow negative)
//
// The witnesses below cover the "true" cases at consteval; the
// "false" (overflowing) cases are pinned by neg-compile fixtures
// (one for each of unsigned-wrap and signed-asymmetric, mirroring
// the no_overflow_mul HS14 discipline).

// Trivial: 0 + 0 cannot overflow any signed or unsigned type.
static_assert(dc::no_overflow_sum<uint32_t>(0, 0));
static_assert(dc::no_overflow_sum<int32_t>(0, 0));

// Boundary — exact max: x + (MAX - x) = MAX is representable.
static_assert(dc::no_overflow_sum<uint8_t>(1, 254));                   // 255 = UINT8_MAX
static_assert(!dc::no_overflow_sum<uint8_t>(1, 255));                  // 256 — overflow
static_assert(dc::no_overflow_sum<uint32_t>(0xFFFF'FFFEu, 1u));        // UINT32_MAX
static_assert(!dc::no_overflow_sum<uint32_t>(std::numeric_limits<uint32_t>::max(), 1u));

// Boundary — exact min for signed.
static_assert(dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::max() - 1, 1));
static_assert(!dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::max(), 1));     // INT32_MAX+1
static_assert(dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min() + 1, -1));
static_assert(!dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min(), -1));    // INT32_MIN-1

// Negative + positive — sum could land anywhere; safe at all interior points.
static_assert(dc::no_overflow_sum<int32_t>(-100, 50));
static_assert(dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));  // -1, fits

// 64-bit witnesses.
static_assert(dc::no_overflow_sum<uint64_t>(0xFFFF'FFFF'FFFF'FFFEull, 1ull));
static_assert(!dc::no_overflow_sum<uint64_t>(std::numeric_limits<uint64_t>::max(), 1ull));
static_assert(!dc::no_overflow_sum<int64_t>(std::numeric_limits<int64_t>::min(), -1));   // INT64_MIN-1

// ── Composition with CRUCIBLE_PRE — production usage shape for sum ─

[[nodiscard]] constexpr uint64_t safe_add_u64(uint64_t a, uint64_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_sum(a, b));
    return a + b;
}

[[nodiscard]] constexpr int32_t safe_add_i32(int32_t a, int32_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_sum(a, b));
    return a + b;
}

static_assert(safe_add_u64(40, 2) == 42);
static_assert(safe_add_i32(-100, 50) == -50);
static_assert(safe_add_i32(std::numeric_limits<int32_t>::max() - 1, 1)
              == std::numeric_limits<int32_t>::max());

// ── no_overflow_pow2_shift positive witnesses ──────────────────────
//
// Coverage matrix (shift = b; bitwidth = W):
//
//   uint8_t       | a=0,b=0     | a=1,b=7 (top bit ok)  | b=8 (UB)         | a=128,b=1 (overflow)
//   uint32_t      | a=1,b=0     | a=1,b=31 (top bit ok) | b=32 (UB)        | a=UINT32_MAX,b=1 (overflow)
//   int32_t       | a=1,b=30 ok | a=1,b=31 (sign-bit UB)| b=-1 (UB)        | a=-1,b=1 (UB neg-shift)
//   int64_t       | a=42,b=0    | b=63 ok if a<=0       | b=64 (UB)        | a=INT64_MAX,b=1 (overflow)
//
// Predicate is total over all (a, b): no UB at the predicate itself.

// Trivial: anything << 0 doesn't overflow (no change).
static_assert(dc::no_overflow_pow2_shift<uint32_t>(0, 0));
static_assert(dc::no_overflow_pow2_shift<uint32_t>(std::numeric_limits<uint32_t>::max(), 0));
static_assert(dc::no_overflow_pow2_shift<int32_t>(std::numeric_limits<int32_t>::max(), 0));
static_assert(dc::no_overflow_pow2_shift<int32_t>(0, 31));   // 0 shifted is still 0

// Shift count out of range — UB at the operator, predicate rejects.
static_assert(!dc::no_overflow_pow2_shift<uint8_t>(1, 8));
static_assert(!dc::no_overflow_pow2_shift<uint32_t>(1, 32));
static_assert(!dc::no_overflow_pow2_shift<uint64_t>(1, 64));
static_assert(!dc::no_overflow_pow2_shift<int32_t>(1, 32));

// Negative shift count — UB on signed b at the operator.
static_assert(!dc::no_overflow_pow2_shift<int32_t>(1, -1));
static_assert(!dc::no_overflow_pow2_shift<int64_t>(42, -10));

// Negative left-operand — UB on signed T at the operator.
static_assert(!dc::no_overflow_pow2_shift<int32_t>(-1, 1));
static_assert(!dc::no_overflow_pow2_shift<int32_t>(std::numeric_limits<int32_t>::min(), 0));

// Boundary: exact-max representable shifts.
static_assert(dc::no_overflow_pow2_shift<uint8_t>(1, 7));                                     // 0x80, fits
static_assert(!dc::no_overflow_pow2_shift<uint8_t>(2, 7));                                    // 0x100, overflow
static_assert(dc::no_overflow_pow2_shift<uint32_t>(1, 31));                                   // 0x80000000, fits unsigned
static_assert(!dc::no_overflow_pow2_shift<uint32_t>(2, 31));                                  // 0x100000000, overflow
static_assert(dc::no_overflow_pow2_shift<int32_t>(1, 30));                                    // 0x40000000, fits signed
static_assert(!dc::no_overflow_pow2_shift<int32_t>(1, 31));                                   // 0x80000000, would set sign bit
static_assert(!dc::no_overflow_pow2_shift<int32_t>(std::numeric_limits<int32_t>::max(), 1));  // INT32_MAX << 1 wraps

// Unsigned wraparound — defined behavior at the C++ level, but
// predicate still detects information loss (top bit shifted out).
static_assert(!dc::no_overflow_pow2_shift<uint32_t>(std::numeric_limits<uint32_t>::max(), 1));

// 64-bit witnesses.
static_assert(dc::no_overflow_pow2_shift<uint64_t>(1ull, 63));
static_assert(!dc::no_overflow_pow2_shift<uint64_t>(2ull, 63));
static_assert(!dc::no_overflow_pow2_shift<int64_t>(std::numeric_limits<int64_t>::max(), 1));

// ── Composition with CRUCIBLE_PRE — shift wrapper ─────────────────

[[nodiscard]] constexpr uint32_t safe_shl_u32(uint32_t a, uint32_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_pow2_shift(a, b));
    return a << b;
}

[[nodiscard]] constexpr int32_t safe_shl_i32(int32_t a, int32_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_pow2_shift(a, b));
    return a << b;
}

static_assert(safe_shl_u32(7, 3) == 56);
static_assert(safe_shl_u32(1, 31) == 0x8000'0000u);
static_assert(safe_shl_i32(1, 30) == 0x4000'0000);
static_assert(safe_shl_i32(0, 31) == 0);

// ── all_in_range positive witnesses ────────────────────────────────
//
// Coverage matrix (signedness × cardinality × locality of violation):
//
//   empty span                         | true (vacuous)
//   1 element, in range                | true
//   1 element, below lo                | false
//   1 element, above hi                | false
//   3 elements, all in range           | true
//   3 elements, first out of range     | false
//   3 elements, middle out of range    | false  ← anti-pattern catch
//   3 elements, last out of range      | false
//   degenerate range (lo == hi)        | element-equality test
//   inverted range (lo > hi)           | empty=true, non-empty=false
//   signed: negative bounds            | uniformly handled
//   uint64: full-range width           | no overflow inside predicate

// Empty span: vacuous truth.
constexpr int32_t empty_arr[] = {0};  // sentinel, span is built with size=0
static_assert(dc::all_in_range<int32_t>(std::span<const int32_t>(empty_arr, 0), 0, 100));
static_assert(dc::all_in_range<int32_t>(std::span<const int32_t>(empty_arr, 0), 100, 0));  // inverted, still vacuous

// Single element witnesses.
constexpr int32_t one_in[] = {42};
constexpr int32_t one_below[] = {-1};
constexpr int32_t one_above[] = {101};
static_assert(dc::all_in_range<int32_t>(one_in, 0, 100));
static_assert(!dc::all_in_range<int32_t>(one_below, 0, 100));
static_assert(!dc::all_in_range<int32_t>(one_above, 0, 100));

// Three-element witnesses — tests the "middle violation" anti-pattern catch.
constexpr int32_t three_in[] = {10, 50, 90};
constexpr int32_t three_first_oor[] = {-1, 50, 90};
constexpr int32_t three_middle_oor[] = {10, 200, 90};
constexpr int32_t three_last_oor[] = {10, 50, 200};
static_assert(dc::all_in_range<int32_t>(three_in, 0, 100));
static_assert(!dc::all_in_range<int32_t>(three_first_oor, 0, 100));
static_assert(!dc::all_in_range<int32_t>(three_middle_oor, 0, 100));   // would slip past endpoint-only check
static_assert(!dc::all_in_range<int32_t>(three_last_oor, 0, 100));

// Boundary: degenerate range (lo == hi) acts as "all elements equal lo".
constexpr int32_t three_eq[] = {7, 7, 7};
constexpr int32_t three_neq[] = {7, 8, 7};
static_assert(dc::all_in_range<int32_t>(three_eq, 7, 7));
static_assert(!dc::all_in_range<int32_t>(three_neq, 7, 7));

// Inverted range (lo > hi): empty span is vacuously true; any non-empty fails.
static_assert(!dc::all_in_range<int32_t>(three_in, 100, 0));

// Signed with negative bounds.
constexpr int32_t signed_in[] = {-50, 0, 50};
constexpr int32_t signed_oor[] = {-50, 0, 200};
static_assert(dc::all_in_range<int32_t>(signed_in, -100, 100));
static_assert(!dc::all_in_range<int32_t>(signed_oor, -100, 100));

// uint64_t full-range edge.
constexpr uint64_t u64_full[] = {0ull, 1ull, std::numeric_limits<uint64_t>::max()};
static_assert(dc::all_in_range<uint64_t>(u64_full, 0ull, std::numeric_limits<uint64_t>::max()));

// ── Composition with CRUCIBLE_PRE — span-quantified production shape ─

[[nodiscard]] constexpr int32_t safe_lookup_i32(std::span<const int32_t> ids,
                                                int32_t lo, int32_t hi,
                                                std::size_t idx) noexcept {
    CRUCIBLE_PRE(dc::all_in_range(ids, lo, hi));
    CRUCIBLE_PRE(idx < ids.size());
    return ids[idx];
}

static_assert(safe_lookup_i32(three_in, 0, 100, 0) == 10);
static_assert(safe_lookup_i32(three_in, 0, 100, 1) == 50);
static_assert(safe_lookup_i32(three_in, 0, 100, 2) == 90);

// ── strictly_increasing positive witnesses ─────────────────────────
//
// Coverage matrix (cardinality × monotonicity-type × locality of violation):
//
//   empty span                     | true (vacuous, no pairs)
//   1 element                      | true (vacuous, no pairs)
//   2 strict                       | true
//   2 equal (duplicate)            | false  ← strict semantic
//   2 decreasing                   | false
//   3 strict                       | true
//   3 with first-pair equal        | false
//   3 with last-pair equal         | false  ← endpoint-only would miss
//   3 with regression              | false
//   uint64_t step_id sequence      | full-range monotonic
//   int64_t with negative bounds   | unified handling

constexpr int32_t empty_int32[] = {0};
constexpr int32_t single_int32[] = {42};
static_assert(dc::strictly_increasing<int32_t>(std::span<const int32_t>(empty_int32, 0)));
static_assert(dc::strictly_increasing<int32_t>(single_int32));

// 2-element witnesses.
constexpr int32_t two_strict[] = {1, 2};
constexpr int32_t two_equal[] = {7, 7};        // duplicate — strict rejects
constexpr int32_t two_decr[] = {3, 1};
static_assert(dc::strictly_increasing<int32_t>(two_strict));
static_assert(!dc::strictly_increasing<int32_t>(two_equal));
static_assert(!dc::strictly_increasing<int32_t>(two_decr));

// 3-element witnesses — duplicate at first / last / middle positions.
constexpr int32_t three_strict[] = {1, 2, 3};
constexpr int32_t three_first_eq[] = {7, 7, 9};
constexpr int32_t three_last_eq[] = {1, 5, 5};       // endpoint-shortcut would miss this
constexpr int32_t three_regress[] = {1, 5, 3};
static_assert(dc::strictly_increasing<int32_t>(three_strict));
static_assert(!dc::strictly_increasing<int32_t>(three_first_eq));
static_assert(!dc::strictly_increasing<int32_t>(three_last_eq));
static_assert(!dc::strictly_increasing<int32_t>(three_regress));

// uint64_t step_id sequence (CONTRACT-107 production shape preview).
constexpr uint64_t step_ids[] = {1ull, 100ull, 10'000ull, 999'999'999ull};
static_assert(dc::strictly_increasing<uint64_t>(step_ids));

// int64_t with negative bounds — strict ordering holds across sign flip.
constexpr int64_t signed_strict[] = {-100, -50, 0, 50, 100};
static_assert(dc::strictly_increasing<int64_t>(signed_strict));

// ── Composition with CRUCIBLE_PRE — sequence-quantified production shape ─

[[nodiscard]] constexpr uint64_t safe_last_step(std::span<const uint64_t> ids) noexcept {
    CRUCIBLE_PRE(dc::strictly_increasing(ids));
    CRUCIBLE_PRE(!ids.empty());
    return ids.back();
}

static_assert(safe_last_step(step_ids) == 999'999'999ull);

// ── weakly_increasing positive witnesses ───────────────────────────
//
// The `<=` shape: duplicates ARE permitted between consecutive
// elements; only STRICT regression (xs[i-1] > xs[i]) is rejected.
//
// Coverage matrix mirrors strictly_increasing's shape, but inverts
// the equal-pair witnesses: where strictly_increasing rejects them,
// weakly_increasing accepts them.  This is the load-bearing
// distinction — same shape, looser predicate, distinct semantic
// commitment about admissible duplicates.

// Vacuous truth — empty and singleton spans satisfy the predicate.
static_assert(dc::weakly_increasing<int32_t>(std::span<const int32_t>{}));
constexpr int32_t weakly_single[] = {42};
static_assert(dc::weakly_increasing<int32_t>(weakly_single));

// 2-element witnesses — equal pair PASSES (vs. strictly_increasing).
constexpr int32_t weakly_two_strict[] = {1, 2};
constexpr int32_t weakly_two_equal[] = {7, 7};       // duplicate — weak ACCEPTS
constexpr int32_t weakly_two_decr[] = {3, 1};
static_assert(dc::weakly_increasing<int32_t>(weakly_two_strict));
static_assert(dc::weakly_increasing<int32_t>(weakly_two_equal));
static_assert(!dc::weakly_increasing<int32_t>(weakly_two_decr));

// 3-element witnesses — duplicates at first / last / middle positions
// all PASS; only strict regression rejects.
constexpr int32_t weakly_three_first_eq[] = {7, 7, 9};
constexpr int32_t weakly_three_last_eq[] = {1, 5, 5};
constexpr int32_t weakly_three_middle_eq[] = {1, 5, 5};
constexpr int32_t weakly_three_regress[] = {1, 5, 3};
static_assert(dc::weakly_increasing<int32_t>(weakly_three_first_eq));
static_assert(dc::weakly_increasing<int32_t>(weakly_three_last_eq));
static_assert(dc::weakly_increasing<int32_t>(weakly_three_middle_eq));
static_assert(!dc::weakly_increasing<int32_t>(weakly_three_regress));

// All-equal sequence — extreme case where every pair stalls.  Strict
// would reject all of these; weak accepts every length.
constexpr uint32_t all_zeros[] = {0u, 0u, 0u, 0u, 0u};
static_assert(dc::weakly_increasing<uint32_t>(all_zeros));
constexpr uint32_t all_max[] = {std::numeric_limits<uint32_t>::max(),
                                std::numeric_limits<uint32_t>::max()};
static_assert(dc::weakly_increasing<uint32_t>(all_max));

// CONTRACT-110 production preview — TraceGraph CSR row-pointer
// offsets where two adjacent rows of length zero share their start
// offset.  This is the canonical scenario where weakly_increasing
// is the RIGHT cite (vs. strictly_increasing which would force a
// pre-filter for empty rows).
constexpr uint32_t row_offsets[] = {0u, 5u, 5u, 5u, 12u, 12u, 20u};
static_assert(dc::weakly_increasing<uint32_t>(row_offsets));

// Endpoint-shortcut counterexample: a sequence whose front <= back
// but contains an INTERIOR strict regression must be rejected.
constexpr uint32_t middle_regress_witness[] = {0u, 5u, 3u, 7u};
static_assert(!dc::weakly_increasing<uint32_t>(middle_regress_witness));

// First-K-pairs counterexample: all but the final pair are weakly
// increasing; partial-scan would silently accept.
constexpr uint32_t tail_regress_witness[] = {1u, 2u, 3u, 5u, 4u};
static_assert(!dc::weakly_increasing<uint32_t>(tail_regress_witness));

// int64_t with negative bounds — weak ordering across sign flip,
// with duplicates admitted at -50 and 0.
constexpr int64_t weakly_signed[] = {-100, -50, -50, 0, 0, 50};
static_assert(dc::weakly_increasing<int64_t>(weakly_signed));

// ── Composition with CRUCIBLE_PRE — sequence-quantified production shape ─

[[nodiscard]] constexpr uint32_t safe_last_offset(std::span<const uint32_t> offs) noexcept {
    CRUCIBLE_PRE(dc::weakly_increasing(offs));
    CRUCIBLE_PRE(!offs.empty());
    return offs.back();
}

static_assert(safe_last_offset(row_offsets) == 20u);
static_assert(safe_last_offset(all_zeros) == 0u);

// ── is_power_of_two_le positive witnesses ──────────────────────────
//
// Conjunction predicate: x ∈ {1, 2, 4, 8, ...} ∩ [1, bound].
// Coverage matrix exercises BOTH conjuncts independently:
//
//   power-of-two       │ within bound │ result
//   ───────────────────┼──────────────┼────────
//   yes (1, 2, 4, 16)  │ yes          │ TRUE
//   yes (256)          │ no           │ FALSE  (above bound)
//   no  (3, 5, 6, 12)  │ yes          │ FALSE  (not power-of-two)
//   no  (0)            │ vacuously    │ FALSE  (zero rejected)
//   no  (-1, INT_MIN)  │ n/a (signed) │ FALSE  (negative rejected)

// Power-of-two cases within bound — accepted.
static_assert(dc::is_power_of_two_le<uint32_t>(1u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(2u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(4u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(8u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(16u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(32u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(64u, 64u));     // x == bound ok

// Power-of-two cases ABOVE bound — rejected (one conjunct fires).
static_assert(!dc::is_power_of_two_le<uint32_t>(128u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(256u, 64u));
static_assert(!dc::is_power_of_two_le<uint64_t>(
    uint64_t{1} << 32, uint64_t{1} << 30));

// Non-power-of-two values within bound — rejected (other conjunct fires).
static_assert(!dc::is_power_of_two_le<uint32_t>(3u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(5u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(6u, 64u));     // even but not power-of-two
static_assert(!dc::is_power_of_two_le<uint32_t>(7u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(10u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(12u, 64u));    // 8 + 4
static_assert(!dc::is_power_of_two_le<uint32_t>(48u, 64u));    // 32 + 16, MISTAKEN as "even and ≤ bound"

// Zero — REJECTED (canonical "0 is not a power of two" convention).
static_assert(!dc::is_power_of_two_le<uint32_t>(0u, 64u));
static_assert(!dc::is_power_of_two_le<uint64_t>(0ull, 1024ull));

// Negative values on signed T — REJECTED.
static_assert(!dc::is_power_of_two_le<int32_t>(-1, 64));
static_assert(!dc::is_power_of_two_le<int32_t>(-128, 64));
static_assert(!dc::is_power_of_two_le<int32_t>(
    std::numeric_limits<int32_t>::min(), 64));   // INT_MIN — would UB on (x-1) without the x<=0 guard

// Signed positive cases — same shape as unsigned.
static_assert(dc::is_power_of_two_le<int32_t>(1, 64));
static_assert(dc::is_power_of_two_le<int32_t>(64, 64));
static_assert(!dc::is_power_of_two_le<int32_t>(48, 64));
static_assert(!dc::is_power_of_two_le<int32_t>(128, 64));

// Empty / degenerate bound — every input rejected.
static_assert(!dc::is_power_of_two_le<uint32_t>(1u, 0u));        // bound = 0
static_assert(!dc::is_power_of_two_le<int32_t>(1, -1));          // bound < 0

// Boundary at uint64 max range.
static_assert(dc::is_power_of_two_le<uint64_t>(
    uint64_t{1} << 63, std::numeric_limits<uint64_t>::max()));

// CONTRACT-109 production preview — SwissCtrl kGroupWidth shape.
// kGroupWidth ∈ {16, 32, 64} (one of three SIMD widths); valid
// values must satisfy is_power_of_two_le(w, 64).
constexpr std::size_t valid_widths[] = {16, 32, 64};
template <std::size_t... Is>
constexpr bool all_valid_widths(std::index_sequence<Is...>) noexcept {
    return (... && dc::is_power_of_two_le<std::size_t>(valid_widths[Is], 64));
}
static_assert(all_valid_widths(std::make_index_sequence<3>{}));

// ── Composition with CRUCIBLE_PRE — capacity-validation production shape ─

[[nodiscard]] constexpr std::size_t
safe_table_capacity(std::size_t w) noexcept {
    CRUCIBLE_PRE(dc::is_power_of_two_le<std::size_t>(w, 64));
    return w;
}

static_assert(safe_table_capacity(16) == 16);
static_assert(safe_table_capacity(32) == 32);
static_assert(safe_table_capacity(64) == 64);

// ── factorization_eq positive witnesses ────────────────────────────
//
// Span-quantified MULTIPLICATIVE check (vs. the additive
// no_overflow_sum and elementwise all_in_range).  Detects:
//   * factor product matches total           — accept
//   * factor product mismatches total        — reject
//   * factor product overflows during scan   — reject (saturating)
//
// Empty span: empty product = 1 (multiplicative identity).
// `factorization_eq([], 1) == true`, others false.

// Empty span — empty product is 1.
static_assert(dc::factorization_eq<uint32_t>(std::span<const uint32_t>{}, 1u));
static_assert(!dc::factorization_eq<uint32_t>(std::span<const uint32_t>{}, 0u));
static_assert(!dc::factorization_eq<uint32_t>(std::span<const uint32_t>{}, 5u));

// Singleton spans — product equals the lone factor.
constexpr uint32_t single_two[] = {2u};
static_assert(dc::factorization_eq<uint32_t>(single_two, 2u));
static_assert(!dc::factorization_eq<uint32_t>(single_two, 4u));

// Trivial 2-factor cases.
constexpr uint32_t two_three[] = {2u, 3u};
static_assert(dc::factorization_eq<uint32_t>(two_three, 6u));
static_assert(!dc::factorization_eq<uint32_t>(two_three, 5u));
static_assert(!dc::factorization_eq<uint32_t>(two_three, 7u));

// Multiplicative identity factors — order doesn't matter, 1s ignored.
constexpr uint32_t with_ones[] = {1u, 4u, 1u, 2u, 1u};
static_assert(dc::factorization_eq<uint32_t>(with_ones, 8u));
static_assert(!dc::factorization_eq<uint32_t>(with_ones, 4u));

// Zero factor — product collapses to zero.
constexpr uint32_t with_zero[] = {2u, 0u, 5u};
static_assert(dc::factorization_eq<uint32_t>(with_zero, 0u));
static_assert(!dc::factorization_eq<uint32_t>(with_zero, 10u));

// CONTRACT-110 production preview — 5D parallelism factor
// decomposition.  TP × DP × PP × EP × CP must equal world_size.
constexpr uint32_t partition_64[] = {2u, 4u, 4u, 1u, 2u};   // 64
static_assert(dc::factorization_eq<uint32_t>(partition_64, 64u));
static_assert(!dc::factorization_eq<uint32_t>(partition_64, 32u));
static_assert(!dc::factorization_eq<uint32_t>(partition_64, 128u));

// 5D partition where TP = 1 (no tensor parallelism) — degenerate
// but valid; product still must equal world_size.
constexpr uint32_t partition_no_tp[] = {1u, 8u, 4u, 1u, 2u};   // 64
static_assert(dc::factorization_eq<uint32_t>(partition_no_tp, 64u));

// Overflow detection — running product overflows uint32_t.
constexpr uint32_t overflowing[] = {65536u, 65536u, 2u};   // 2^33
static_assert(!dc::factorization_eq<uint32_t>(overflowing, 0u));
static_assert(!dc::factorization_eq<uint32_t>(overflowing, 8589934592ull
                                              & 0xFFFFFFFFu));    // wrapped value

// Even if the wrapped product happens to coincide with `total`
// numerically, the predicate REJECTS — overflow is a categorical
// failure, not a value-equality check.
constexpr uint32_t hits_zero_via_wrap[] = {65536u, 65536u};   // 2^32 wraps to 0
static_assert(!dc::factorization_eq<uint32_t>(hits_zero_via_wrap, 0u));

// uint64_t — large factorizations stay in range.
constexpr uint64_t partition_1m[] = {16ull, 16ull, 16ull, 16ull, 16ull};  // 16^5 = 2^20
static_assert(dc::factorization_eq<uint64_t>(partition_1m, 1ull << 20));

// Signed T — negative factors flip sign.
constexpr int32_t signed_factors[] = {-2, 3, -4};   // (-2)*3*(-4) = 24
static_assert(dc::factorization_eq<int32_t>(signed_factors, 24));
static_assert(!dc::factorization_eq<int32_t>(signed_factors, -24));

// ── Composition with CRUCIBLE_PRE — partition-validation production shape ─

[[nodiscard]] constexpr uint32_t
safe_partition_world_size(std::span<const uint32_t> dims, uint32_t world) noexcept {
    CRUCIBLE_PRE(dc::factorization_eq(dims, world));
    return world;
}

static_assert(safe_partition_world_size(partition_64, 64u) == 64u);
static_assert(safe_partition_world_size(partition_no_tp, 64u) == 64u);

// ── coprime positive witnesses ─────────────────────────────────────
//
// Coprimality test via Euclidean algorithm.  Edge cases:
//   * (0, 0)   → false (gcd is 0)
//   * (0, 1)   → true  (gcd is 1)
//   * (1, n)   → true  (1 coprime to everything)
//   * (n, n)   → true iff n == 1
//   * (-a, b)  → same as (|a|, |b|)
//
// Coverage matrix:
//   shared prime factor      → false (15, 25 share 5)
//   no shared factor         → true  (8, 9 are coprime)
//   one is 1                 → true
//   identical non-1          → false (7, 7 share 7)
//   zero edge cases pinned   → various

// Trivial cases.
static_assert(dc::coprime<uint32_t>(1u, 1u));
static_assert(dc::coprime<uint32_t>(1u, 999u));
static_assert(dc::coprime<uint32_t>(999u, 1u));

// (0, 0) — false by definition (gcd(0,0) = 0).
static_assert(!dc::coprime<uint32_t>(0u, 0u));
static_assert(!dc::coprime<int32_t>(0, 0));

// (0, n) — gcd(0, n) = n; coprime iff n == 1.
static_assert(dc::coprime<uint32_t>(0u, 1u));
static_assert(dc::coprime<uint32_t>(1u, 0u));
static_assert(!dc::coprime<uint32_t>(0u, 2u));
static_assert(!dc::coprime<uint32_t>(0u, 999u));

// (n, n) — share self as factor; coprime iff n == 1.
static_assert(!dc::coprime<uint32_t>(2u, 2u));
static_assert(!dc::coprime<uint32_t>(7u, 7u));
static_assert(!dc::coprime<uint32_t>(999u, 999u));

// Coprime pairs — no shared prime factor.
static_assert(dc::coprime<uint32_t>(2u, 3u));
static_assert(dc::coprime<uint32_t>(3u, 5u));
static_assert(dc::coprime<uint32_t>(8u, 9u));         // 2^3 vs 3^2
static_assert(dc::coprime<uint32_t>(7u, 11u));
static_assert(dc::coprime<uint32_t>(35u, 64u));       // 5*7 vs 2^6
static_assert(dc::coprime<uint32_t>(13u, 21u));       // 13 vs 3*7

// Non-coprime pairs — shared prime factor.
static_assert(!dc::coprime<uint32_t>(6u, 8u));        // gcd 2
static_assert(!dc::coprime<uint32_t>(15u, 25u));      // gcd 5
static_assert(!dc::coprime<uint32_t>(77u, 91u));      // gcd 7 (7*11 vs 7*13)
static_assert(!dc::coprime<uint32_t>(12u, 18u));      // gcd 6
static_assert(!dc::coprime<uint32_t>(100u, 30u));     // gcd 10

// Anti-pattern witness: 6 % 9 != 0 AND 9 % 6 != 0, but NOT coprime.
// `pre(a % b != 0 && b % a != 0)` would silently accept this.
static_assert(!dc::coprime<uint32_t>(6u, 9u));        // gcd 3

// Signed T — sign doesn't affect coprimality.
static_assert(dc::coprime<int32_t>(8, 9));
static_assert(dc::coprime<int32_t>(-8, 9));
static_assert(dc::coprime<int32_t>(8, -9));
static_assert(dc::coprime<int32_t>(-8, -9));
static_assert(!dc::coprime<int32_t>(15, -25));
static_assert(!dc::coprime<int32_t>(-15, 25));

// INT_MIN — predicate must be total despite the |INT_MIN| > INT_MAX
// edge case.  INT_MIN = -2^31; |INT_MIN| as unsigned is 2^31.
// gcd(2^31, 1) == 1, so coprime(INT_MIN, 1) == true.  gcd(2^31, 2) == 2,
// so coprime(INT_MIN, 2) == false.
static_assert(dc::coprime<int32_t>(std::numeric_limits<int32_t>::min(), 1));
static_assert(!dc::coprime<int32_t>(std::numeric_limits<int32_t>::min(), 2));
static_assert(!dc::coprime<int32_t>(std::numeric_limits<int32_t>::min(),
                                    std::numeric_limits<int32_t>::min()));

// uint64_t large primes — coprime by construction.
static_assert(dc::coprime<uint64_t>(982451653ull, 982451707ull));   // two distinct primes

// CONTRACT-109 production preview — Swiss-table double-hashing
// secondary stride must be coprime to table capacity for the
// probe sequence to visit every slot.  Capacity 64 = 2^6, so
// any odd stride is coprime; any even stride shares factor 2.
static_assert(dc::coprime<uint32_t>(7u, 64u));        // 7 odd, coprime to 2^6
static_assert(dc::coprime<uint32_t>(13u, 64u));
static_assert(!dc::coprime<uint32_t>(8u, 64u));       // gcd 8
static_assert(!dc::coprime<uint32_t>(48u, 64u));      // gcd 16

// ── Composition with CRUCIBLE_PRE — secondary-stride production shape ─

[[nodiscard]] constexpr uint32_t
safe_secondary_stride(uint32_t stride, uint32_t capacity) noexcept {
    CRUCIBLE_PRE(dc::coprime<uint32_t>(stride, capacity));
    return stride;
}

static_assert(safe_secondary_stride(7u, 64u) == 7u);
static_assert(safe_secondary_stride(13u, 100u) == 13u);

}  // namespace

// ── Runtime smoke test ─────────────────────────────────────────────
//
// All static_asserts above proved consteval correctness; this main()
// ensures the !NDEBUG runtime path executes cleanly (a contract
// violation under !NDEBUG would SIGABRT, surfacing as a non-zero exit
// code to CTest).  No printf — silent on success, exit 0 indicates
// pass.

int main() {
    int volatile sink = 0;

    // Trigger the predicate at runtime (not constexpr-folded) by
    // routing through volatile sinks.
    uint64_t volatile a = 100;
    uint64_t volatile b = 200;
    sink += static_cast<int>(safe_mul_u64(a, b));   // 20000

    int32_t volatile c = -5;
    int32_t volatile d = 7;
    sink += safe_mul_i32(c, d);                     // -35

    int32_t volatile e = 46340;
    int32_t volatile f = 46340;
    sink += safe_mul_i32(e, f) / 100'000;           // 21473

    // Direct predicate calls (no CRUCIBLE_PRE wrapper).
    if (!dc::no_overflow_mul<uint32_t>(100u, 200u)) {
        std::fprintf(stderr, "test_decide: 100u*200u flagged as overflow\n");
        return 1;
    }
    if (dc::no_overflow_mul<uint32_t>(std::numeric_limits<uint32_t>::max(), 2u)) {
        std::fprintf(stderr, "test_decide: UINT32_MAX*2 NOT flagged as overflow\n");
        return 1;
    }

    // no_overflow_sum runtime witnesses — same shape as mul above.
    uint64_t volatile sa = 1000;
    uint64_t volatile sb = 2000;
    sink += static_cast<int>(safe_add_u64(sa, sb));         // 3000

    int32_t volatile si = -7;
    int32_t volatile sj = 12;
    sink += safe_add_i32(si, sj);                            // 5

    if (!dc::no_overflow_sum<uint32_t>(100u, 200u)) {
        std::fprintf(stderr, "test_decide: 100u+200u flagged as overflow\n");
        return 1;
    }
    if (dc::no_overflow_sum<uint32_t>(std::numeric_limits<uint32_t>::max(), 1u)) {
        std::fprintf(stderr, "test_decide: UINT32_MAX+1 NOT flagged as overflow\n");
        return 1;
    }
    if (dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min(), -1)) {
        std::fprintf(stderr, "test_decide: INT32_MIN+(-1) NOT flagged as overflow\n");
        return 1;
    }

    // no_overflow_pow2_shift runtime witnesses.
    uint32_t volatile sa_shl = 7;
    uint32_t volatile sb_shl = 3;
    sink += static_cast<int>(safe_shl_u32(sa_shl, sb_shl));   // 56

    int32_t volatile si_shl = 1;
    int32_t volatile sj_shl = 30;
    sink += safe_shl_i32(si_shl, sj_shl) / 100'000'000;       // 10

    // Direct predicate calls covering the four UB classes.
    if (!dc::no_overflow_pow2_shift<uint32_t>(1u, 31u)) {
        std::fprintf(stderr, "test_decide: 1u<<31 flagged as overflow\n");
        return 1;
    }
    if (dc::no_overflow_pow2_shift<uint32_t>(1u, 32u)) {
        std::fprintf(stderr, "test_decide: shift-count-32 NOT flagged\n");
        return 1;
    }
    if (dc::no_overflow_pow2_shift<int32_t>(int32_t{-1}, int32_t{1})) {
        std::fprintf(stderr, "test_decide: signed-negative-shift NOT flagged\n");
        return 1;
    }
    if (dc::no_overflow_pow2_shift<int32_t>(int32_t{1}, int32_t{31})) {
        std::fprintf(stderr, "test_decide: 1<<31 (signed) NOT flagged as sign-bit overflow\n");
        return 1;
    }

    // all_in_range runtime witnesses: routed through volatile sinks
    // to defeat constant-folding of the span construction.
    int32_t volatile sink_arr[3] = {10, 50, 90};
    int32_t arr_copy[3] = {sink_arr[0], sink_arr[1], sink_arr[2]};
    std::span<const int32_t> arr_span{arr_copy, 3};
    sink += static_cast<int>(dc::all_in_range<int32_t>(arr_span, 0, 100));   // 1
    sink += static_cast<int>(dc::all_in_range<int32_t>(arr_span, 0, 50));    // 0 (90 > 50)

    if (!dc::all_in_range<uint32_t>(std::span<const uint32_t>{}, 0u, 0u)) {
        std::fprintf(stderr, "test_decide: empty span NOT vacuously true\n");
        return 1;
    }
    constexpr int32_t middle_violator[] = {10, 200, 90};
    if (dc::all_in_range<int32_t>(middle_violator, 0, 100)) {
        std::fprintf(stderr, "test_decide: middle-violator NOT detected\n");
        return 1;
    }

    // strictly_increasing runtime witnesses.
    uint64_t volatile sa_seq[4] = {1, 2, 3, 4};
    uint64_t seq_copy[4] = {sa_seq[0], sa_seq[1], sa_seq[2], sa_seq[3]};
    sink += static_cast<int>(dc::strictly_increasing<uint64_t>(
        std::span<const uint64_t>{seq_copy, 4}));   // 1

    if (!dc::strictly_increasing<int32_t>(std::span<const int32_t>{})) {
        std::fprintf(stderr, "test_decide: empty-span NOT vacuously increasing\n");
        return 1;
    }
    constexpr uint64_t stalled[] = {1, 2, 2, 3};
    if (dc::strictly_increasing<uint64_t>(stalled)) {
        std::fprintf(stderr, "test_decide: stalled-pair (2,2) NOT detected\n");
        return 1;
    }
    constexpr int64_t regressed[] = {10, 5};
    if (dc::strictly_increasing<int64_t>(regressed)) {
        std::fprintf(stderr, "test_decide: regression NOT detected\n");
        return 1;
    }

    // weakly_increasing runtime witnesses — the equal-pair case is
    // the load-bearing distinction vs. strictly_increasing.
    uint32_t volatile sa_offs[5] = {0, 5, 5, 12, 20};
    uint32_t offs_copy[5] = {sa_offs[0], sa_offs[1], sa_offs[2], sa_offs[3], sa_offs[4]};
    sink += static_cast<int>(dc::weakly_increasing<uint32_t>(
        std::span<const uint32_t>{offs_copy, 5}));   // 1 — equal pair OK

    if (!dc::weakly_increasing<int32_t>(std::span<const int32_t>{})) {
        std::fprintf(stderr, "test_decide: weakly empty NOT vacuously true\n");
        return 1;
    }
    constexpr uint32_t weakly_stalled_ok[] = {1, 2, 2, 3};
    if (!dc::weakly_increasing<uint32_t>(weakly_stalled_ok)) {
        std::fprintf(stderr, "test_decide: weakly stalled-pair (2,2) WRONGLY rejected\n");
        return 1;
    }
    constexpr uint32_t weakly_middle_regress[] = {0, 5, 3, 7};
    if (dc::weakly_increasing<uint32_t>(weakly_middle_regress)) {
        std::fprintf(stderr, "test_decide: weakly middle regress NOT detected\n");
        return 1;
    }
    constexpr uint32_t weakly_tail_regress[] = {1, 2, 3, 5, 4};
    if (dc::weakly_increasing<uint32_t>(weakly_tail_regress)) {
        std::fprintf(stderr, "test_decide: weakly tail regress NOT detected\n");
        return 1;
    }

    // is_power_of_two_le runtime witnesses.
    std::size_t volatile sa_w = 32;
    sink += static_cast<int>(safe_table_capacity(static_cast<std::size_t>(sa_w)));   // 32

    if (!dc::is_power_of_two_le<uint32_t>(16u, 64u)) {
        std::fprintf(stderr, "test_decide: 16 ≤ 64 (power of 2) WRONGLY rejected\n");
        return 1;
    }
    if (dc::is_power_of_two_le<uint32_t>(48u, 64u)) {
        std::fprintf(stderr, "test_decide: 48 (= 32+16, not pow2) WRONGLY accepted\n");
        return 1;
    }
    if (dc::is_power_of_two_le<uint32_t>(128u, 64u)) {
        std::fprintf(stderr, "test_decide: 128 > 64 WRONGLY accepted\n");
        return 1;
    }
    if (dc::is_power_of_two_le<uint32_t>(0u, 64u)) {
        std::fprintf(stderr, "test_decide: 0 WRONGLY accepted as power of 2\n");
        return 1;
    }
    if (dc::is_power_of_two_le<int32_t>(-1, 64)) {
        std::fprintf(stderr, "test_decide: -1 WRONGLY accepted as power of 2\n");
        return 1;
    }
    if (dc::is_power_of_two_le<int32_t>(std::numeric_limits<int32_t>::min(), 64)) {
        std::fprintf(stderr, "test_decide: INT_MIN WRONGLY accepted (UB-on-x-1 risk)\n");
        return 1;
    }

    // factorization_eq runtime witnesses.
    uint32_t volatile sa_dims[5] = {2, 4, 4, 1, 2};
    uint32_t dims_copy[5] = {sa_dims[0], sa_dims[1], sa_dims[2], sa_dims[3], sa_dims[4]};
    sink += static_cast<int>(safe_partition_world_size(
        std::span<const uint32_t>{dims_copy, 5}, 64u));   // 64

    if (!dc::factorization_eq<uint32_t>(std::span<const uint32_t>{}, 1u)) {
        std::fprintf(stderr, "test_decide: empty product != 1\n");
        return 1;
    }
    if (dc::factorization_eq<uint32_t>(std::span<const uint32_t>{}, 0u)) {
        std::fprintf(stderr, "test_decide: empty product WRONGLY equals 0\n");
        return 1;
    }
    constexpr uint32_t bad_partition[] = {8u, 2u, 4u, 1u, 2u};   // product = 128
    if (dc::factorization_eq<uint32_t>(bad_partition, 64u)) {
        std::fprintf(stderr, "test_decide: 128 != 64 WRONGLY accepted\n");
        return 1;
    }
    constexpr uint32_t overflow_factors[] = {65536u, 65536u};   // 2^32, wraps to 0
    if (dc::factorization_eq<uint32_t>(overflow_factors, 0u)) {
        std::fprintf(stderr, "test_decide: overflow-wrap WRONGLY accepted\n");
        return 1;
    }

    // coprime runtime witnesses.
    uint32_t volatile sa_stride = 7;
    uint32_t volatile sa_cap = 64;
    sink += static_cast<int>(safe_secondary_stride(
        static_cast<uint32_t>(sa_stride), static_cast<uint32_t>(sa_cap)));   // 7

    if (!dc::coprime<uint32_t>(8u, 9u)) {
        std::fprintf(stderr, "test_decide: coprime(8,9) WRONGLY rejected\n");
        return 1;
    }
    if (dc::coprime<uint32_t>(15u, 25u)) {
        std::fprintf(stderr, "test_decide: coprime(15,25) WRONGLY accepted\n");
        return 1;
    }
    if (dc::coprime<uint32_t>(0u, 0u)) {
        std::fprintf(stderr, "test_decide: coprime(0,0) WRONGLY accepted\n");
        return 1;
    }
    // Anti-pattern witness: pre(a%b != 0 && b%a != 0) accepts (6,9).
    // The full Euclidean predicate rejects.
    if (dc::coprime<uint32_t>(6u, 9u)) {
        std::fprintf(stderr, "test_decide: coprime(6,9) WRONGLY accepted (one-sided modulo bug)\n");
        return 1;
    }
    // INT_MIN must not UB.
    if (dc::coprime<int32_t>(std::numeric_limits<int32_t>::min(), 2)) {
        std::fprintf(stderr, "test_decide: coprime(INT_MIN, 2) WRONGLY accepted\n");
        return 1;
    }

    if (sink == 0) {
        std::fprintf(stderr, "test_decide: sink unexpectedly zero\n");
        return 1;
    }
    return 0;
}
