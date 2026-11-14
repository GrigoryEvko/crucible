// The add, the subtract, the multiply and the negate of foundation/Simd.h,
// at lanes that hold the limits of each lane type.  In the type of a signed
// lane, such an operation at a limit overflows, and a signed overflow is
// undefined behavior.  The test builds with UBSan and
// -fno-sanitize-recover=all.  Most presets also build with
// -fno-strict-overflow, which defines a signed overflow as a wrap, so UBSan
// finds no overflow there.  The preset ubsan-strict removes that flag, and
// there an overflow ends the test.  Each lane of a result must also equal
// the scalar operation in the unsigned type of the lane, which wraps modulo
// two to the width of the lane.
//
// The constant evaluations at the end hold the same facts at compile time,
// where a signed overflow is not a constant expression.

#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/core/Number.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace simd = ::foundation::simd;
namespace fc = ::foundation::core;

// The value comes back through a call that no optimization looks into, so
// each operation below runs on a run-time vector.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

// The values of the lanes of one operand: the greatest value, the least
// value, minus one, zero and one, in turn from the first lane.
template <typename T>
[[nodiscard]] constexpr T limit_value(int position) noexcept {
    switch (position % 5) {
        case 0:
            return fc::max_of<T>;
        case 1:
            return fc::min_of<T>;
        case 2:
            return static_cast<T>(~T{0});
        case 3:
            return T{0};
        default:
            return T{1};
    }
}

// The scalar operations in the unsigned type of the lane.
enum class Operation : std::uint8_t {
    add,
    subtract,
    multiply,
};

// Each operation computes in 64 unsigned bits.  An unsigned lane narrower
// than int would promote to int, where the product of two greatest values
// of 16 bits overflows.  The conversion to the unsigned type of the lane
// then keeps the low bits, which is the wrap of the lane.
template <typename T>
[[nodiscard]] constexpr T wrap_scalar(Operation operation, T left, T right) noexcept {
    using Unsigned = std::make_unsigned_t<T>;
    const auto left_bits = static_cast<std::uint64_t>(static_cast<Unsigned>(left));
    const auto right_bits = static_cast<std::uint64_t>(static_cast<Unsigned>(right));
    if (operation == Operation::add) return static_cast<T>(static_cast<Unsigned>(left_bits + right_bits));
    if (operation == Operation::subtract) return static_cast<T>(static_cast<Unsigned>(left_bits - right_bits));
    return static_cast<T>(static_cast<Unsigned>(left_bits * right_bits));
}

template <typename T>
[[nodiscard]] constexpr T wrap_negate(T value) noexcept {
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
    return static_cast<T>(static_cast<Unsigned>(std::uint64_t{0} - bits));
}

// Each operation on each pair of the five limit values, against the scalar
// operation.  The right operand of lane l is the value of position
// l + shift, so the shifts 0 to 4 give each pair.
template <typename T, int Lanes>
void check_operations(int shift) {
    using V = simd::vec<T, Lanes>;
    const int right_shift = opaque(shift);
    const V left = opaque(V([](auto lane) noexcept { return limit_value<T>(decltype(lane)::value); }));
    const V right =
        opaque(V([right_shift](auto lane) noexcept { return limit_value<T>(decltype(lane)::value + right_shift); }));

    const V sum = left + right;
    const V difference = left - right;
    const V product = left * right;
    const V negation = -left;
    for (int lane = 0; lane < Lanes; ++lane) {
        const T left_value = limit_value<T>(lane);
        const T right_value = limit_value<T>(lane + shift);
        CRUCIBLE_FATAL_INVARIANT(sum[lane] == wrap_scalar(Operation::add, left_value, right_value));
        CRUCIBLE_FATAL_INVARIANT(difference[lane] == wrap_scalar(Operation::subtract, left_value, right_value));
        CRUCIBLE_FATAL_INVARIANT(product[lane] == wrap_scalar(Operation::multiply, left_value, right_value));
        CRUCIBLE_FATAL_INVARIANT(negation[lane] == wrap_negate(left_value));
    }
}

// Each shift, for a vector of 64 bytes and for a vector of two lanes.
template <typename T>
void check_lane_type() {
    constexpr int wide = static_cast<int>(64 / sizeof(T));
    for (int shift = 0; shift < 5; ++shift) {
        check_operations<T, wide>(shift);
        check_operations<T, 2>(shift);
    }
}

// The same facts in a constant evaluation, for the 32-bit lane.
using I32x4 = simd::vec<std::int32_t, 4>;
constexpr I32x4 greatest{fc::max_of<std::int32_t>};
constexpr I32x4 least{fc::min_of<std::int32_t>};
static_assert((greatest + I32x4{1})[0] == fc::min_of<std::int32_t>);
static_assert((least - I32x4{1})[0] == fc::max_of<std::int32_t>);
static_assert((greatest * I32x4{2})[0] == -2);
static_assert((-least)[0] == fc::min_of<std::int32_t>);

}  // namespace

int main() {
    check_lane_type<std::int8_t>();
    check_lane_type<std::uint8_t>();
    check_lane_type<std::int16_t>();
    check_lane_type<std::uint16_t>();
    check_lane_type<std::int32_t>();
    check_lane_type<std::uint32_t>();
    check_lane_type<std::int64_t>();
    check_lane_type<std::uint64_t>();
    return 0;
}
