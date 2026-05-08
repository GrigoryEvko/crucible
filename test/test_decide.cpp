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

    if (sink == 0) {
        std::fprintf(stderr, "test_decide: sink unexpectedly zero\n");
        return 1;
    }
    return 0;
}
