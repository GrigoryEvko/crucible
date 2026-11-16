// The compile-time checks of foundation/core/Float.h.

#include <foundation/core/Float.h>

#include <foundation/core/Scalar.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::float_checks {

// total_key is one to one, so two values with one key have the same bits.
// -Werror=float-equal refuses == of two floats.
template <class T>
[[nodiscard]] consteval bool same_bits(T left, T right) {
    return fp::total_key(left) == fp::total_key(right);
}

// Each operation gives the type of its operand, and the key is the word of
// the bits.
static_assert(std::is_same_v<decltype(fp::abs(1.0f)), float> && std::is_same_v<decltype(fp::sqrt(1.0)), double>);
static_assert(std::is_same_v<decltype(fp::total_key(1.0f)), std::uint32_t>);
static_assert(std::is_same_v<decltype(fp::total_key(1.0)), std::uint64_t>);

// The classes.  A signaling NaN is a NaN, 0 and a subnormal value are finite
// and not normal, and an infinity is not finite.
static_assert(fp::is_nan(quiet_nan_of<double>) && fp::is_nan(signaling_nan_of<float>));
static_assert(!fp::is_nan(infinity_of<double>) && !fp::is_nan(max_of<float>) && !fp::is_nan(0.0));
static_assert(fp::is_inf(infinity_of<float>) && fp::is_inf(-infinity_of<double>));
static_assert(!fp::is_inf(max_of<double>) && !fp::is_inf(quiet_nan_of<double>));
static_assert(fp::is_finite(0.0) && fp::is_finite(min_subnormal_of<double>) && fp::is_finite(min_of<float>));
static_assert(!fp::is_finite(infinity_of<double>) && !fp::is_finite(quiet_nan_of<float>));
static_assert(fp::is_normal(min_normal_of<double>) && fp::is_normal(-1.0f) && fp::is_normal(max_of<double>));
static_assert(!fp::is_normal(0.0) && !fp::is_normal(min_subnormal_of<float>) && !fp::is_normal(infinity_of<double>));
static_assert(!fp::is_normal(quiet_nan_of<double>));
static_assert(fp::sign_bit(-0.0) && !fp::sign_bit(0.0) && fp::sign_bit(-1.0f) && fp::sign_bit(-infinity_of<double>));
static_assert(fp::sign_bit(-quiet_nan_of<double>) && !fp::sign_bit(quiet_nan_of<float>));

// canonicalize_nan gives the canonical NaN for each NaN: a payload, a sign, a
// signaling NaN.  Each other value keeps its bits, the infinities and -0 too.
static_assert(same_bits(fp::canonicalize_nan(bit_cast<float>(0xFFC12345u)), quiet_nan_of<float>));
static_assert(same_bits(fp::canonicalize_nan(signaling_nan_of<double>), quiet_nan_of<double>));
static_assert(same_bits(fp::canonicalize_nan(bit_cast<float>(0x7F800001u)), quiet_nan_of<float>));
static_assert(same_bits(fp::canonicalize_nan(-quiet_nan_of<double>), quiet_nan_of<double>));
static_assert(same_bits(fp::canonicalize_nan(-infinity_of<float>), -infinity_of<float>));
static_assert(same_bits(fp::canonicalize_nan(infinity_of<double>), infinity_of<double>));
static_assert(same_bits(fp::canonicalize_nan(-0.0), -0.0)
              && same_bits(fp::canonicalize_nan(max_of<float>), max_of<float>));

// abs and copy_sign change only the sign bit, so a NaN keeps its payload.
static_assert(same_bits(fp::abs(-0.0), 0.0) && same_bits(fp::abs(-2.5f), 2.5f));
static_assert(same_bits(fp::abs(-quiet_nan_of<double>), quiet_nan_of<double>));
static_assert(same_bits(fp::abs(-infinity_of<float>), infinity_of<float>));
static_assert(same_bits(fp::copy_sign(2.0, -0.0), -2.0) && same_bits(fp::copy_sign(-2.0f, 1.0f), 2.0f));
static_assert(same_bits(fp::copy_sign(quiet_nan_of<double>, -1.0), -quiet_nan_of<double>));
static_assert(same_bits(fp::copy_sign(0.0, -quiet_nan_of<double>), -0.0));

// sqrt gives the canonical quiet NaN for each negative value and each NaN,
// and -0 for -0.
static_assert(same_bits(fp::sqrt(4.0), 2.0) && same_bits(fp::sqrt(2.25f), 1.5f) && same_bits(fp::sqrt(0.0), 0.0));
static_assert(same_bits(fp::sqrt(-0.0), -0.0) && same_bits(fp::sqrt(-0.0f), -0.0f));
static_assert(same_bits(fp::sqrt(-1.0), quiet_nan_of<double>));
static_assert(same_bits(fp::sqrt(-min_subnormal_of<float>), quiet_nan_of<float>));
static_assert(same_bits(fp::sqrt(-infinity_of<double>), quiet_nan_of<double>));
static_assert(same_bits(fp::sqrt(-quiet_nan_of<double>), quiet_nan_of<double>));
static_assert(same_bits(fp::sqrt(signaling_nan_of<float>), quiet_nan_of<float>));

// fma rounds one time: 0.1 * 10 rounds to 1, but the exact product is
// 2^-54 more than 1.
static_assert(fp::fma(0.1, 10.0, -1.0) > 0.0 && same_bits(fp::fma(0.1, 10.0, -1.0), 0x1p-54));
static_assert(same_bits(fp::fma(3.0f, 4.0f, 5.0f), 17.0f) && same_bits(fp::fma(-2.0, 0.0, 0.0), 0.0));

// A constant evaluation of fma runs the exact form in integers (THE FMA).
// test_core_fma compares the form at run time with an exact oracle, and with
// the instruction of a second unit of the program.
//
// The product (1 + 2^-12)^2 = 1 + 2^-11 + 2^-24 lies half way between two
// floats.  A tiny addend of either sign decides the round, and an addend of
// one unit makes a tie that goes to the even neighbor.  The product
// (1 + 2^-26)(1 + 2^-27) = 1 + 3 * 2^-27 + 2^-53 is the same case for a
// double.
static_assert(same_bits(fp::fma(1.0f + 0x1p-12f, 1.0f + 0x1p-12f, 0x1p-60f), 1.0f + 0x1p-11f + 0x1p-23f));
static_assert(same_bits(fp::fma(1.0f + 0x1p-12f, 1.0f + 0x1p-12f, -0x1p-60f), 1.0f + 0x1p-11f));
static_assert(same_bits(fp::fma(1.0f + 0x1p-12f, 1.0f + 0x1p-12f, 0x1p-23f), 1.0f + 0x1p-11f + 0x1p-22f));
static_assert(same_bits(fp::fma(1.0 + 0x1p-26, 1.0 + 0x1p-27, 0x1p-200), 1.0 + 0x3p-27 + 0x1p-52));
static_assert(same_bits(fp::fma(1.0 + 0x1p-26, 1.0 + 0x1p-27, -0x1p-200), 1.0 + 0x3p-27));
static_assert(same_bits(fp::fma(1.0 + 0x1p-26, 1.0 + 0x1p-27, 0x1p-52), 1.0 + 0x3p-27 + 0x1p-51));

// The addend cancels the low bits of the product and loses its last bit in
// the move, so only the sticky bit keeps the exact sum below the tie.
static_assert(same_bits(fp::fma(1.0 + 0x1p-31 + 0x1p-52, 1.0 + 0x1p-22, -(0x1p-74 + 0x1p-126)), 0x1.0000040200001p0));

// A large cancellation keeps each bit of the product: (1 + u)(1 - u) - 1 is
// -u^2 exactly.
static_assert(same_bits(fp::fma(1.0f + 0x1p-23f, 1.0f - 0x1p-23f, -1.0f), -0x1p-46f));
static_assert(same_bits(fp::fma(1.0 + 0x1p-52, 1.0 - 0x1p-52, -1.0), -0x1p-104));
static_assert(same_bits(fp::fma(0.1f, 10.0f, -1.0f), 0x1p-26f));

// A product that underflows: 1.5 * 2^-75 squared is 1.125 * 2^-149.  It rounds
// to the smallest subnormal alone.  An add of minus that subnormal gives the
// exact sum 0.125 * 2^-149, which rounds to +0, and the negated product gives
// -0.125 * 2^-149, which rounds to -0.
static_assert(same_bits(fp::fma(0x1.8p-75f, 0x1.8p-75f, 0.0f), min_subnormal_of<float>));
static_assert(same_bits(fp::fma(0x1.8p-75f, 0x1.8p-75f, -min_subnormal_of<float>), 0.0f));
static_assert(same_bits(fp::fma(-0x1.8p-75f, 0x1.8p-75f, min_subnormal_of<float>), -0.0f));
static_assert(same_bits(fp::fma(0x1.8p-75f, 0x1.8p-75f, min_normal_of<float>),
                        __builtin_bit_cast(float, FloatBits<float>::min_normal + 1u)));
static_assert(same_bits(fp::fma(0x1.8p-538, 0x1.8p-538, -min_subnormal_of<double>), -0.0));
static_assert(same_bits(fp::fma(0x1.8p-537, 0x1.8p-537, min_subnormal_of<double>), 0x3p-1074));

// The signs of a zero follow the add of IEEE 754 under the round to nearest.
static_assert(same_bits(fp::fma(-1.0f, 0.0f, -0.0f), -0.0f) && same_bits(fp::fma(-1.0f, 0.0f, 0.0f), 0.0f));
static_assert(same_bits(fp::fma(1.0, 1.0, -1.0), 0.0) && same_bits(fp::fma(-0x1p-80f, 0x1p-80f, 0.0f), -0.0f));
static_assert(same_bits(fp::fma(-0x1p-600, 0x1p-600, 0.0), -0.0));
static_assert(same_bits(fp::fma(-0x1p-600, 0x1p-600, min_subnormal_of<double>), min_subnormal_of<double>));

// The product of the largest float and 2 is past the largest float, but the
// add of minus the largest float brings the exact sum back to it.  A product
// past the range with no such add is an infinity, and an infinite addend
// gives itself.
static_assert(same_bits(fp::fma(max_of<float>, 2.0f, -max_of<float>), max_of<float>));
static_assert(same_bits(fp::fma(max_of<double>, 2.0, -max_of<double>), max_of<double>));
static_assert(same_bits(fp::fma(max_of<float>, 2.0f, 0x1p100f), infinity_of<float>));
static_assert(same_bits(fp::fma(max_of<double>, -2.0, -1.0), -infinity_of<double>));
static_assert(same_bits(fp::fma(max_of<float>, 2.0f, -infinity_of<float>), -infinity_of<float>));
static_assert(same_bits(fp::fma(1.0, 2.0, infinity_of<double>), infinity_of<double>));

// The total order: each negative NaN, -inf, the negative values, -0, +0,
// the positive values, +inf, and each positive NaN.  A NaN with a larger
// payload comes after a NaN with a smaller payload.
static_assert(fp::total_key(-quiet_nan_of<double>) < fp::total_key(-signaling_nan_of<double>));
static_assert(fp::total_key(-signaling_nan_of<double>) < fp::total_key(-infinity_of<double>));
static_assert(fp::total_key(-infinity_of<double>) < fp::total_key(min_of<double>));
static_assert(fp::total_key(-1.0) < fp::total_key(-min_subnormal_of<double>));
static_assert(fp::total_key(-min_subnormal_of<double>) < fp::total_key(-0.0));
static_assert(fp::total_key(-0.0) < fp::total_key(0.0) && fp::total_key(0.0) == 0x8000000000000000u);
static_assert(fp::total_key(0.0) < fp::total_key(min_subnormal_of<double>));
static_assert(fp::total_key(1.0) < fp::total_key(max_of<double>));
static_assert(fp::total_key(max_of<double>) < fp::total_key(infinity_of<double>));
static_assert(fp::total_key(infinity_of<double>) < fp::total_key(signaling_nan_of<double>));
static_assert(fp::total_key(signaling_nan_of<double>) < fp::total_key(quiet_nan_of<double>));
static_assert(fp::total_key(-0.0f) == 0x7FFFFFFFu && fp::total_key(-quiet_nan_of<float>) == 0x003FFFFFu);

}  // namespace foundation::core::detail::float_checks
