// The compile-time checks of foundation/core/Float.h.

#include <foundation/core/Float.h>

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
