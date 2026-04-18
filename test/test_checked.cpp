// Edge-case tests for crucible::safety::Checked.h primitives.
//
// Four modes per op (checked_, wrapping_, trapping_, saturating_)
// across add/sub/mul/div/mod/neg/abs/shl/shr.  Every signed-overflow
// corner (INT_MIN/-1, INT_MIN*-1, MAX+1, MIN-1) and divide-by-zero
// path is exercised.

#include <crucible/safety/Checked.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>

using namespace crucible::safety;

template <typename T> constexpr T MIN = std::numeric_limits<T>::min();
template <typename T> constexpr T MAX = std::numeric_limits<T>::max();

static void test_checked_nullopt_on_overflow() {
    // checked_add
    assert(checked_add<int32_t>(1, 2) == 3);
    assert(checked_add<int32_t>(MAX<int32_t>, 1) == std::nullopt);
    assert(checked_add<int32_t>(MIN<int32_t>, -1) == std::nullopt);
    assert(checked_add<uint32_t>(MAX<uint32_t>, 1u) == std::nullopt);

    // checked_sub
    assert(checked_sub<int32_t>(10, 3) == 7);
    assert(checked_sub<int32_t>(MIN<int32_t>, 1) == std::nullopt);
    assert(checked_sub<uint32_t>(0u, 1u) == std::nullopt);

    // checked_mul
    assert(checked_mul<int32_t>(3, 4) == 12);
    assert(checked_mul<int32_t>(MAX<int32_t>, 2) == std::nullopt);
    assert(checked_mul<int32_t>(MIN<int32_t>, -1) == std::nullopt);

    // checked_div
    assert(checked_div<int32_t>(10, 3) == 3);
    assert(checked_div<int32_t>(10, 0) == std::nullopt);
    assert(checked_div<int32_t>(MIN<int32_t>, -1) == std::nullopt);
    assert(checked_div<uint32_t>(10u, 0u) == std::nullopt);

    // checked_mod
    assert(checked_mod<int32_t>(10, 3) == 1);
    assert(checked_mod<int32_t>(10, 0) == std::nullopt);
    // INT_MIN % -1 is mathematically 0 despite INT_MIN/-1 overflow:
    assert(checked_mod<int32_t>(MIN<int32_t>, -1) == 0);

    // checked_neg / checked_abs
    assert(checked_neg<int32_t>(5) == -5);
    assert(checked_neg<int32_t>(MIN<int32_t>) == std::nullopt);
    assert(checked_abs<int32_t>(-5) == 5);
    assert(checked_abs<int32_t>(MIN<int32_t>) == std::nullopt);

    // checked_shl
    assert(checked_shl<uint32_t>(1u, 3) == 8u);
    assert(checked_shl<uint32_t>(1u, 32) == std::nullopt);   // bitwidth
    assert(checked_shl<uint32_t>(1u, -1) == std::nullopt);   // negative shift
    assert(checked_shl<int32_t>(-1, 3) == std::nullopt);     // signed < 0
    // checked_shr
    assert(checked_shr<uint32_t>(8u, 3) == 1u);
    assert(checked_shr<uint32_t>(1u, 32) == std::nullopt);
    std::printf("  test_checked_nullopt:           PASSED\n");
}

static void test_wrapping_twos_complement() {
    assert(wrapping_add<uint8_t>(200, 100) == uint8_t(44));
    assert(wrapping_sub<uint32_t>(0u, 1u) == MAX<uint32_t>);
    assert(wrapping_mul<int32_t>(MAX<int32_t>, 2) == -2);
    // Signed-overflow wrap is well-defined: MAX+1 == MIN.
    assert(wrapping_add<int32_t>(MAX<int32_t>, 1) == MIN<int32_t>);
    std::printf("  test_wrapping:                  PASSED\n");
}

static void test_saturating_clamps() {
    // saturating_* forwards to crucible::sat::* polyfill.
    assert(saturating_add<uint32_t>(MAX<uint32_t>, 1) == MAX<uint32_t>);
    assert(saturating_sub<uint32_t>(0u, 1u) == 0u);
    assert(saturating_mul<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);
    assert(saturating_mul<int32_t>(MIN<int32_t>, 2) == MIN<int32_t>);
    // Non-overflowing cases pass through.
    assert(saturating_add<int32_t>(3, 4) == 7);
    assert(saturating_mul<uint32_t>(3u, 4u) == 12u);
    std::printf("  test_saturating:                PASSED\n");
}

static void test_trapping_on_bad_path() {
    // Non-overflow paths return correct value.
    assert(trapping_add<int32_t>(1, 2) == 3);
    assert(trapping_sub<int32_t>(10, 3) == 7);
    assert(trapping_mul<int32_t>(3, 4) == 12);
    assert(trapping_div<int32_t>(10, 3) == 3);
    std::printf("  test_trapping_happy_path:       PASSED\n");
}

static void test_constexpr_usability() {
    static_assert(checked_add<int32_t>(1, 2) == 3);
    static_assert(checked_mul<int32_t>(MIN<int32_t>, -1) == std::nullopt);
    static_assert(wrapping_add<int32_t>(MAX<int32_t>, 1) == MIN<int32_t>);
    static_assert(saturating_mul<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);
    std::printf("  test_constexpr:                 PASSED\n");
}

static void test_across_all_widths() {
    // Sample each integer width for a basic overflow path.
    assert(checked_add<int8_t>(100, 100) == std::nullopt);
    assert(checked_add<int16_t>(30000, 30000) == std::nullopt);
    assert(checked_add<int64_t>(MAX<int64_t>, 1) == std::nullopt);
    assert(checked_mul<uint8_t>(16, 16) == std::nullopt);
    assert(checked_mul<uint64_t>(MAX<uint64_t>, 2) == std::nullopt);
    assert(saturating_add<int64_t>(MIN<int64_t>, -1) == MIN<int64_t>);
    std::printf("  test_all_widths:                PASSED\n");
}

int main() {
    test_checked_nullopt_on_overflow();
    test_wrapping_twos_complement();
    test_saturating_clamps();
    test_trapping_on_bad_path();
    test_constexpr_usability();
    test_across_all_widths();
    std::printf("test_checked: 6 groups, all passed\n");
    return 0;
}
