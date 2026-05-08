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

#include <cstdint>
#include <cstdio>
#include <limits>

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

    if (sink == 0) {
        std::fprintf(stderr, "test_decide: sink unexpectedly zero\n");
        return 1;
    }
    return 0;
}
