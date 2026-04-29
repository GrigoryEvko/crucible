// Edge-case tests for crucible::sat::{add,sub,mul}_sat.
//
// Matches P0543 semantics exactly: clamp to the appropriate bound on
// overflow, identity when no overflow.  Every signed-overflow corner
// case and every unsigned wrap-around direction is exercised.

#include <crucible/Saturate.h>

#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <limits>

using crucible::sat::add_sat;
using crucible::sat::sub_sat;
using crucible::sat::mul_sat;

// Helper: alias the numeric_limits bounds.
template <typename T> constexpr T MIN = std::numeric_limits<T>::min();
template <typename T> constexpr T MAX = std::numeric_limits<T>::max();

static void test_add_unsigned() {
    // Non-overflow.
    assert(add_sat<uint8_t>(10, 20) == 30);
    assert(add_sat<uint16_t>(1000, 2000) == 3000);
    assert(add_sat<uint32_t>(1u, 2u) == 3u);
    assert(add_sat<uint64_t>(1ULL, 2ULL) == 3ULL);

    // Overflow → clamp MAX.
    assert(add_sat<uint8_t>(200, 200) == MAX<uint8_t>);
    assert(add_sat<uint32_t>(MAX<uint32_t>, 1) == MAX<uint32_t>);
    assert(add_sat<uint64_t>(MAX<uint64_t>, 1) == MAX<uint64_t>);
    assert(add_sat<uint64_t>(MAX<uint64_t>, MAX<uint64_t>) == MAX<uint64_t>);

    // Identity.
    assert(add_sat<uint32_t>(0, 0) == 0);
    assert(add_sat<uint32_t>(MAX<uint32_t>, 0) == MAX<uint32_t>);
}

static void test_add_signed() {
    assert(add_sat<int32_t>(1, 2) == 3);
    assert(add_sat<int32_t>(-1, -2) == -3);

    // Overflow high.
    assert(add_sat<int32_t>(MAX<int32_t>, 1) == MAX<int32_t>);
    assert(add_sat<int32_t>(MAX<int32_t>, MAX<int32_t>) == MAX<int32_t>);

    // Overflow low.
    assert(add_sat<int32_t>(MIN<int32_t>, -1) == MIN<int32_t>);
    assert(add_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == MIN<int32_t>);

    // No overflow across zero.
    assert(add_sat<int32_t>(MAX<int32_t>, MIN<int32_t>) == -1);
    assert(add_sat<int32_t>(-5, 10) == 5);
}

static void test_sub_unsigned() {
    assert(sub_sat<uint32_t>(10, 3) == 7);
    assert(sub_sat<uint32_t>(MAX<uint32_t>, 1) == MAX<uint32_t> - 1);

    // Wrap below zero → clamp MIN (= 0 for unsigned).
    assert(sub_sat<uint8_t>(0, 1) == 0);
    assert(sub_sat<uint32_t>(0, 1) == 0);
    assert(sub_sat<uint64_t>(0, MAX<uint64_t>) == 0);
    assert(sub_sat<uint32_t>(5, 10) == 0);
}

static void test_sub_signed() {
    assert(sub_sat<int32_t>(10, 3) == 7);
    assert(sub_sat<int32_t>(3, 10) == -7);

    // a - b with a >= 0: overflow only if result > MAX — clamp MAX.
    assert(sub_sat<int32_t>(0, MIN<int32_t>) == MAX<int32_t>);
    assert(sub_sat<int32_t>(1, MIN<int32_t>) == MAX<int32_t>);
    assert(sub_sat<int32_t>(MAX<int32_t>, -1) == MAX<int32_t>);

    // a - b with a < 0: overflow only if result < MIN — clamp MIN.
    assert(sub_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    assert(sub_sat<int32_t>(MIN<int32_t>, MAX<int32_t>) == MIN<int32_t>);
    assert(sub_sat<int32_t>(-1, MAX<int32_t>) == MIN<int32_t>);

    // INT_MIN - INT_MIN = 0 (mathematically — fits, no overflow).
    assert(sub_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == 0);
    assert(sub_sat<int64_t>(MIN<int64_t>, MIN<int64_t>) == 0);

    // -1 - INT_MAX = INT_MIN exactly (fits on two's complement).
    assert(sub_sat<int32_t>(-1, MAX<int32_t>) == MIN<int32_t>);
}

static void test_mul_unsigned() {
    assert(mul_sat<uint32_t>(3, 4) == 12);
    assert(mul_sat<uint32_t>(0, MAX<uint32_t>) == 0);
    assert(mul_sat<uint32_t>(1, MAX<uint32_t>) == MAX<uint32_t>);

    // Overflow → clamp MAX.
    assert(mul_sat<uint32_t>(MAX<uint32_t>, 2) == MAX<uint32_t>);
    assert(mul_sat<uint64_t>(MAX<uint64_t>, MAX<uint64_t>) == MAX<uint64_t>);
    assert(mul_sat<uint8_t>(16, 16) == MAX<uint8_t>);  // 256 > 255
}

static void test_mul_signed() {
    assert(mul_sat<int32_t>(3, 4) == 12);
    assert(mul_sat<int32_t>(-3, 4) == -12);
    assert(mul_sat<int32_t>(-3, -4) == 12);

    // Zero operand — never overflows regardless of the other operand.
    assert(mul_sat<int32_t>(0, MIN<int32_t>) == 0);
    assert(mul_sat<int32_t>(MIN<int32_t>, 0) == 0);
    assert(mul_sat<int32_t>(MAX<int32_t>, 0) == 0);

    // INT_MIN * 1 = INT_MIN (fits).
    assert(mul_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    assert(mul_sat<int32_t>(1, MIN<int32_t>) == MIN<int32_t>);

    // INT_MIN * -1 = INT_MAX+1 → overflow high → clamp MAX.
    // True product is positive, so XOR-of-signs = false → clamp MAX.
    assert(mul_sat<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);
    assert(mul_sat<int32_t>(-1, MIN<int32_t>) == MAX<int32_t>);
    assert(mul_sat<int64_t>(MIN<int64_t>, -1) == MAX<int64_t>);

    // INT_MIN * 2 = huge negative → clamp MIN.
    assert(mul_sat<int32_t>(MIN<int32_t>, 2) == MIN<int32_t>);
    assert(mul_sat<int32_t>(2, MIN<int32_t>) == MIN<int32_t>);

    // MAX * MAX → clamp MAX (both positive).
    assert(mul_sat<int32_t>(MAX<int32_t>, MAX<int32_t>) == MAX<int32_t>);

    // MIN * MIN → huge positive → clamp MAX (XOR of signs = false).
    assert(mul_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == MAX<int32_t>);

    // Mixed-sign overflow → clamp MIN.
    assert(mul_sat<int32_t>(MAX<int32_t>, -2) == MIN<int32_t>);
    assert(mul_sat<int32_t>(-2, MAX<int32_t>) == MIN<int32_t>);
}

static void test_constexpr_usable() {
    // All three must be usable in constant-expression contexts.
    static_assert(add_sat<uint32_t>(1u, 2u) == 3u);
    static_assert(sub_sat<uint32_t>(5u, 3u) == 2u);
    static_assert(mul_sat<uint32_t>(3u, 4u) == 12u);
    static_assert(add_sat<int32_t>(MAX<int32_t>, 1) == MAX<int32_t>);
    static_assert(sub_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    static_assert(mul_sat<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);
}

int main() {
    test_add_unsigned();
    test_add_signed();
    test_sub_unsigned();
    test_sub_signed();
    test_mul_unsigned();
    test_mul_signed();
    test_constexpr_usable();
    std::printf("test_saturate: all 7 groups passed\n");
    return 0;
}
