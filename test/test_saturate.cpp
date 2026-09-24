// The semantics must match P0543 exactly: clamp to the nearer bound on
// overflow, and return the plain result otherwise.  Every signed corner
// case and both unsigned wrap directions appear below.

#include <crucible/_Saturate.h>

#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>

using crucible::sat::add_sat;
using crucible::sat::add_sat_det;
using crucible::sat::add_sat_from;
using crucible::sat::add_sat_into;
using crucible::sat::sub_sat;
using crucible::sat::sub_sat_det;
using crucible::sat::sub_sat_from;
using crucible::sat::sub_sat_into;
using crucible::sat::mul_sat;
using crucible::sat::mul_sat_det;
using crucible::sat::mul_sat_from;
using crucible::sat::mul_sat_into;

template <typename T>
constexpr T MIN = std::numeric_limits<T>::min();
template <typename T>
constexpr T MAX = std::numeric_limits<T>::max();

static void test_add_unsigned() {
    assert(add_sat<uint8_t>(10, 20) == 30);
    assert(add_sat<uint16_t>(1000, 2000) == 3000);
    assert(add_sat<uint32_t>(1u, 2u) == 3u);
    assert(add_sat<uint64_t>(1ULL, 2ULL) == 3ULL);

    assert(add_sat<uint8_t>(200, 200) == MAX<uint8_t>);
    assert(add_sat<uint32_t>(MAX<uint32_t>, 1) == MAX<uint32_t>);
    assert(add_sat<uint64_t>(MAX<uint64_t>, 1) == MAX<uint64_t>);
    assert(add_sat<uint64_t>(MAX<uint64_t>, MAX<uint64_t>) == MAX<uint64_t>);

    assert(add_sat<uint32_t>(0, 0) == 0);
    assert(add_sat<uint32_t>(MAX<uint32_t>, 0) == MAX<uint32_t>);
}

static void test_add_signed() {
    assert(add_sat<int32_t>(1, 2) == 3);
    assert(add_sat<int32_t>(-1, -2) == -3);

    assert(add_sat<int32_t>(MAX<int32_t>, 1) == MAX<int32_t>);
    assert(add_sat<int32_t>(MAX<int32_t>, MAX<int32_t>) == MAX<int32_t>);

    assert(add_sat<int32_t>(MIN<int32_t>, -1) == MIN<int32_t>);
    assert(add_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == MIN<int32_t>);

    // Opposite signs cannot overflow, however far apart the operands are.
    assert(add_sat<int32_t>(MAX<int32_t>, MIN<int32_t>) == -1);
    assert(add_sat<int32_t>(-5, 10) == 5);
}

static void test_sub_unsigned() {
    assert(sub_sat<uint32_t>(10, 3) == 7);
    assert(sub_sat<uint32_t>(MAX<uint32_t>, 1) == MAX<uint32_t> - 1);

    // The lower bound of an unsigned type is zero, so that is the clamp.
    assert(sub_sat<uint8_t>(0, 1) == 0);
    assert(sub_sat<uint32_t>(0, 1) == 0);
    assert(sub_sat<uint64_t>(0, MAX<uint64_t>) == 0);
    assert(sub_sat<uint32_t>(5, 10) == 0);
}

static void test_sub_signed() {
    assert(sub_sat<int32_t>(10, 3) == 7);
    assert(sub_sat<int32_t>(3, 10) == -7);

    // A non-negative minuend can only overflow upwards.
    assert(sub_sat<int32_t>(0, MIN<int32_t>) == MAX<int32_t>);
    assert(sub_sat<int32_t>(1, MIN<int32_t>) == MAX<int32_t>);
    assert(sub_sat<int32_t>(MAX<int32_t>, -1) == MAX<int32_t>);

    // A negative minuend can only overflow downwards.
    assert(sub_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    assert(sub_sat<int32_t>(MIN<int32_t>, MAX<int32_t>) == MIN<int32_t>);
    assert(sub_sat<int32_t>(-1, MAX<int32_t>) == MIN<int32_t>);

    // A value minus itself is zero, so these do not overflow at all.
    assert(sub_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == 0);
    assert(sub_sat<int64_t>(MIN<int64_t>, MIN<int64_t>) == 0);

    // The most negative value is exactly representable, so this lands on
    // the bound rather than clamping to it.
    assert(sub_sat<int32_t>(-1, MAX<int32_t>) == MIN<int32_t>);
}

static void test_mul_unsigned() {
    assert(mul_sat<uint32_t>(3, 4) == 12);
    assert(mul_sat<uint32_t>(0, MAX<uint32_t>) == 0);
    assert(mul_sat<uint32_t>(1, MAX<uint32_t>) == MAX<uint32_t>);

    assert(mul_sat<uint32_t>(MAX<uint32_t>, 2) == MAX<uint32_t>);
    assert(mul_sat<uint64_t>(MAX<uint64_t>, MAX<uint64_t>) == MAX<uint64_t>);
    assert(mul_sat<uint8_t>(16, 16) == MAX<uint8_t>);  // 256 overflows a byte by one
}

static void test_mul_signed() {
    assert(mul_sat<int32_t>(3, 4) == 12);
    assert(mul_sat<int32_t>(-3, 4) == -12);
    assert(mul_sat<int32_t>(-3, -4) == 12);

    // A zero operand cannot overflow, whatever the other one is.
    assert(mul_sat<int32_t>(0, MIN<int32_t>) == 0);
    assert(mul_sat<int32_t>(MIN<int32_t>, 0) == 0);
    assert(mul_sat<int32_t>(MAX<int32_t>, 0) == 0);

    // Multiplying by one is exact, even at the most negative value.
    assert(mul_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    assert(mul_sat<int32_t>(1, MIN<int32_t>) == MIN<int32_t>);

    // Negating the most negative value overflows by one.  The true
    // product is positive, so the exclusive-or of the operand signs is
    // false and the clamp goes to the upper bound.
    assert(mul_sat<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);
    assert(mul_sat<int32_t>(-1, MIN<int32_t>) == MAX<int32_t>);
    assert(mul_sat<int64_t>(MIN<int64_t>, -1) == MAX<int64_t>);

    // A negative product clamps to the lower bound.
    assert(mul_sat<int32_t>(MIN<int32_t>, 2) == MIN<int32_t>);
    assert(mul_sat<int32_t>(2, MIN<int32_t>) == MIN<int32_t>);

    assert(mul_sat<int32_t>(MAX<int32_t>, MAX<int32_t>) == MAX<int32_t>);

    // Two negative operands give a positive product, so this clamps
    // upwards despite both inputs being at the lower bound.
    assert(mul_sat<int32_t>(MIN<int32_t>, MIN<int32_t>) == MAX<int32_t>);

    assert(mul_sat<int32_t>(MAX<int32_t>, -2) == MIN<int32_t>);
    assert(mul_sat<int32_t>(-2, MAX<int32_t>) == MIN<int32_t>);
}

static void test_constexpr_usable() {
    // Every operation must be usable in a constant expression.
    static_assert(add_sat<uint32_t>(1u, 2u) == 3u);
    static_assert(sub_sat<uint32_t>(5u, 3u) == 2u);
    static_assert(mul_sat<uint32_t>(3u, 4u) == 12u);
    static_assert(add_sat<int32_t>(MAX<int32_t>, 1) == MAX<int32_t>);
    static_assert(sub_sat<int32_t>(MIN<int32_t>, 1) == MIN<int32_t>);
    static_assert(mul_sat<int32_t>(MIN<int32_t>, -1) == MAX<int32_t>);
    static_assert(std::is_same_v<decltype(add_sat_det<uint32_t>(1u, 2u)), crucible::sat::DetSatPure<uint32_t>>);
    static_assert(sizeof(crucible::sat::DetSatPure<uint64_t>) == sizeof(crucible::safety::Saturated<uint64_t>));
    static_assert(add_sat_det<uint8_t>(200, 100).peek().value() == MAX<uint8_t>);
    static_assert(add_sat_det<uint8_t>(200, 100).peek().was_clamped());
    static_assert(!sub_sat_det<uint32_t>(5, 3).peek().was_clamped());
    static_assert(mul_sat_det<int32_t>(MIN<int32_t>, -1).peek().value() == MAX<int32_t>);
    static_assert(
        decltype(mul_sat_det<int32_t>(MIN<int32_t>, -1))::template satisfies<crucible::safety::DetSafeTier_v::Pure>);
    constexpr uint32_t counter = 40;
    static_assert(add_sat_from(counter, 2u).value() == 42u);
    static_assert(!add_sat_from(counter, 2u).was_clamped());
    static_assert(sub_sat_from(counter, 50u).value() == 0u);
    static_assert(sub_sat_from(counter, 50u).was_clamped());
    static_assert(mul_sat_from(counter, 2u).value() == 80u);
}

static void test_det_wrappers() {
    auto add = add_sat_det<uint32_t>(MAX<uint32_t>, 1u);
    assert(add.peek().value() == MAX<uint32_t>);
    assert(add.peek().was_clamped());

    auto sub = sub_sat_det<uint32_t>(10u, 3u);
    assert(sub.peek().value() == 7u);
    assert(!sub.peek().was_clamped());

    auto mul = mul_sat_det<int32_t>(MIN<int32_t>, -1);
    assert(mul.peek().value() == MAX<int32_t>);
    assert(mul.peek().was_clamped());

    auto relaxed = mul.relax<crucible::safety::DetSafeTier_v::PhiloxRng>();
    assert(relaxed.peek().value() == MAX<int32_t>);
}

static void test_memory_counter_wrappers() {
    uint32_t add_counter = MAX<uint32_t> - 1u;
    auto add = add_sat_into(add_counter, 10u);
    assert(add_counter == MAX<uint32_t>);
    assert(add.value() == MAX<uint32_t>);
    assert(add.was_clamped());

    uint32_t sub_counter = 3u;
    auto sub = sub_sat_into(sub_counter, 10u);
    assert(sub_counter == 0u);
    assert(sub.value() == 0u);
    assert(sub.was_clamped());

    uint16_t mul_counter = 1000u;
    auto mul = mul_sat_into<uint16_t>(mul_counter, uint16_t{70});
    assert(mul_counter == MAX<uint16_t>);
    assert(mul.value() == MAX<uint16_t>);
    assert(mul.was_clamped());

    uint32_t read_only = 9u;
    auto projected = mul_sat_from(read_only, 9u);
    assert(read_only == 9u);
    assert(projected.value() == 81u);
    assert(!projected.was_clamped());
}

int main() {
    test_add_unsigned();
    test_add_signed();
    test_sub_unsigned();
    test_sub_signed();
    test_mul_unsigned();
    test_mul_signed();
    test_constexpr_usable();
    test_det_wrappers();
    test_memory_counter_wrappers();
    std::printf("test_saturate: all 9 groups passed\n");
    return 0;
}
