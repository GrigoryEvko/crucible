// The semantics must match P0543 exactly: clamp to the nearer bound on
// overflow, and return the plain result otherwise.  Every signed corner
// case and both unsigned wrap directions appear below.
//
// The first six groups exercise the three plain helpers, which are the
// three that live in foundation.  The groups over the wrapped forms are
// in test/fixy/test_saturate.cpp, beside the header those forms live
// in.  Each check is an EXPECT, so one run reports every failure it has
// rather than the first.  The seventh group compares every 8-bit pair
// against the library.

#include <foundation/Saturate.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <numeric>
#include "../test_assert.h"

using foundation::sat::add_sat;
using foundation::sat::mul_sat;
using foundation::sat::sub_sat;

namespace {

int g_failures = 0;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

template <typename T>
constexpr T MIN = std::numeric_limits<T>::min();
template <typename T>
constexpr T MAX = std::numeric_limits<T>::max();

void test_add_unsigned() {
    EXPECT(add_sat<uint8_t>(10, 20) == 30);
    EXPECT(add_sat<uint16_t>(1000, 2000) == 3000);
    EXPECT(add_sat<uint32_t>(1u, 2u) == 3u);
    EXPECT(add_sat<uint64_t>(1ULL, 2ULL) == 3ULL);

    EXPECT(add_sat<uint8_t>(200, 200) == MAX<uint8_t>);
    EXPECT(add_sat<uint32_t>(MAX<uint32_t>, 1) == MAX<uint32_t>);
    EXPECT(add_sat<uint64_t>(MAX<uint64_t>, 1) == MAX<uint64_t>);
    EXPECT(add_sat<uint64_t>(MAX<uint64_t>, MAX<uint64_t>) == MAX<uint64_t>);

    EXPECT(add_sat<uint32_t>(0, 0) == 0);
    EXPECT(add_sat<uint32_t>(MAX<uint32_t>, 0) == MAX<uint32_t>);
}

void test_add_signed() {
    EXPECT(add_sat<int32_t>(1, 2) == 3);
    EXPECT(add_sat<int32_t>(-1, -2) == -3);

    EXPECT(add_sat<int32_t>(MAX<int32_t>, 1) == MAX<int32_t>);
    EXPECT(add_sat<int32_t>(MAX<int32_t>, MAX<int32_t>) == MAX<int32_t>);

    EXPECT(add_sat<int32_t>(MIN<int32_t>, -1) == MIN<int32_t>);
    EXPECT(add_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == MIN<int32_t>);

    // Opposite signs cannot overflow, however far apart the operands are.
    EXPECT(add_sat<int32_t>(MAX<int32_t>, MIN<int32_t>) == -1);
    EXPECT(add_sat<int32_t>(-5, 10) == 5);
}

void test_sub_unsigned() {
    EXPECT(sub_sat<uint32_t>(10, 3) == 7);
    EXPECT(sub_sat<uint32_t>(MAX<uint32_t>, 1) == MAX<uint32_t> - 1);

    // The lower bound of an unsigned type is zero, so that is the clamp.
    EXPECT(sub_sat<uint8_t>(0, 1) == 0);
    EXPECT(sub_sat<uint32_t>(0, 1) == 0);
    EXPECT(sub_sat<uint64_t>(0, MAX<uint64_t>) == 0);
    EXPECT(sub_sat<uint32_t>(5, 10) == 0);
}

void test_sub_signed() {
    EXPECT(sub_sat<int32_t>(10, 3) == 7);
    EXPECT(sub_sat<int32_t>(3, 10) == -7);

    // A non-negative minuend can only overflow upwards.
    EXPECT(sub_sat<int32_t>(0, MIN<int32_t>) == MAX<int32_t>);
    EXPECT(sub_sat<int32_t>(1, MIN<int32_t>) == MAX<int32_t>);
    EXPECT(sub_sat<int32_t>(MAX<int32_t>, -1) == MAX<int32_t>);

    // A negative minuend can only overflow downwards.
    EXPECT(sub_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    EXPECT(sub_sat<int32_t>(MIN<int32_t>, MAX<int32_t>) == MIN<int32_t>);
    EXPECT(sub_sat<int32_t>(-1, MAX<int32_t>) == MIN<int32_t>);

    // A value minus itself is zero, so these do not overflow at all.
    EXPECT(sub_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == 0);
    EXPECT(sub_sat<int64_t>(MIN<int64_t>, MIN<int64_t>) == 0);

    // The most negative value is exactly representable, so this lands on
    // the bound rather than clamping to it.
    EXPECT(sub_sat<int32_t>(-1, MAX<int32_t>) == MIN<int32_t>);
}

void test_mul_unsigned() {
    EXPECT(mul_sat<uint32_t>(3, 4) == 12);
    EXPECT(mul_sat<uint32_t>(0, MAX<uint32_t>) == 0);
    EXPECT(mul_sat<uint32_t>(1, MAX<uint32_t>) == MAX<uint32_t>);

    EXPECT(mul_sat<uint32_t>(MAX<uint32_t>, 2) == MAX<uint32_t>);
    EXPECT(mul_sat<uint64_t>(MAX<uint64_t>, MAX<uint64_t>) == MAX<uint64_t>);
    EXPECT(mul_sat<uint8_t>(16, 16) == MAX<uint8_t>);  // 256 overflows a byte by one
}

void test_mul_signed() {
    EXPECT(mul_sat<int32_t>(3, 4) == 12);
    EXPECT(mul_sat<int32_t>(-3, 4) == -12);
    EXPECT(mul_sat<int32_t>(-3, -4) == 12);

    // A zero operand cannot overflow, whatever the other one is.
    EXPECT(mul_sat<int32_t>(0, MIN<int32_t>) == 0);
    EXPECT(mul_sat<int32_t>(MIN<int32_t>, 0) == 0);
    EXPECT(mul_sat<int32_t>(MAX<int32_t>, 0) == 0);

    // Multiplying by one is exact, even at the most negative value.
    EXPECT(mul_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    EXPECT(mul_sat<int32_t>(1, MIN<int32_t>) == MIN<int32_t>);

    // Negating the most negative value overflows by one.  The true
    // product is positive, so the exclusive-or of the operand signs is
    // false and the clamp goes to the upper bound.
    EXPECT(mul_sat<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);
    EXPECT(mul_sat<int32_t>(-1, MIN<int32_t>) == MAX<int32_t>);
    EXPECT(mul_sat<int64_t>(MIN<int64_t>, -1) == MAX<int64_t>);

    // A negative product clamps to the lower bound.
    EXPECT(mul_sat<int32_t>(MIN<int32_t>, 2) == MIN<int32_t>);
    EXPECT(mul_sat<int32_t>(2, MIN<int32_t>) == MIN<int32_t>);

    EXPECT(mul_sat<int32_t>(MAX<int32_t>, MAX<int32_t>) == MAX<int32_t>);

    // Two negative operands give a positive product, so this clamps
    // upwards despite both inputs being at the lower bound.
    EXPECT(mul_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == MAX<int32_t>);

    EXPECT(mul_sat<int32_t>(MAX<int32_t>, -2) == MIN<int32_t>);
    EXPECT(mul_sat<int32_t>(-2, MAX<int32_t>) == MIN<int32_t>);
}

// Each helper returns the exact result on its own path and calls the
// library only on overflow.  Every pair of 8-bit operands, signed and
// unsigned, must give the result the library gives, so neither path can
// drift from P0543.  The sweep is 2 x 65536 pairs for each operation.
template <typename T>
void sweep_against_library() {
    for (int lhs = MIN<T>; lhs <= MAX<T>; ++lhs) {
        for (int rhs = MIN<T>; rhs <= MAX<T>; ++rhs) {
            const T a = static_cast<T>(lhs);
            const T b = static_cast<T>(rhs);
            EXPECT(add_sat<T>(a, b) == std::saturating_add(a, b));
            EXPECT(sub_sat<T>(a, b) == std::saturating_sub(a, b));
            EXPECT(mul_sat<T>(a, b) == std::saturating_mul(a, b));
        }
    }
}

void test_sweep_8_bit() {
    sweep_against_library<int8_t>();
    sweep_against_library<uint8_t>();
}

// Every operation must be usable in a constant expression.
static_assert(add_sat<uint32_t>(1u, 2u) == 3u);
static_assert(sub_sat<uint32_t>(5u, 3u) == 2u);
static_assert(mul_sat<uint32_t>(3u, 4u) == 12u);
static_assert(add_sat<int32_t>(MAX<int32_t>, 1) == MAX<int32_t>);
static_assert(sub_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
static_assert(mul_sat<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);

}  // namespace

int main() {
    test_add_unsigned();
    test_add_signed();
    test_sub_unsigned();
    test_sub_signed();
    test_mul_unsigned();
    test_mul_signed();
    test_sweep_8_bit();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_saturate: %d failure(s)\n", g_failures);
        return 1;
    }
    crucible::test::pass("test_saturate: all 7 groups passed\n");
    return 0;
}
