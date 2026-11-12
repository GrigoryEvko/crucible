// The compile-time checks of foundation/core/Number.h.

#include <foundation/core/Number.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::number_checks {

enum class Phase : std::uint8_t {
    first,
    second
};
struct Pair {
    int first = 0;
    int second = 0;
};
using Count = std::uint32_t;

// The gates.  A character type, bool, a cv-qualified type, an enumeration
// and a class are not an Integer.
static_assert(Integer<signed char> && Integer<unsigned char> && Integer<short> && Integer<unsigned short>);
static_assert(Integer<int> && Integer<unsigned> && Integer<long> && Integer<unsigned long>);
static_assert(Integer<long long> && Integer<unsigned long long> && Integer<Count>);
static_assert(!Integer<bool> && !Integer<char> && !Integer<wchar_t> && !Integer<char8_t>);
static_assert(!Integer<char16_t> && !Integer<char32_t> && !Integer<int const> && !Integer<int volatile>);
static_assert(!Integer<int&> && !Integer<Phase> && !Integer<Pair> && !Integer<float>);
static_assert(SignedInteger<std::int8_t> && SignedInteger<std::int64_t> && !SignedInteger<std::uint8_t>);
static_assert(UnsignedInteger<std::uint8_t> && UnsignedInteger<std::size_t> && !UnsignedInteger<std::int32_t>);
static_assert(Float<float> && Float<double> && !Float<long double> && !Float<int> && !Float<float const>);
static_assert(Arithmetic<int> && Arithmetic<double> && !Arithmetic<bool> && !Arithmetic<long double>);
static_assert(!Arithmetic<Phase> && !Arithmetic<Pair> && !Arithmetic<int*> && !Arithmetic<char>);

// The limits of the integers.  long and long long are two types of one
// width on the two targets.
static_assert(max_of<std::uint8_t> == 255 && min_of<std::uint8_t> == 0);
static_assert(max_of<std::int8_t> == 127 && min_of<std::int8_t> == -128);
static_assert(max_of<std::uint16_t> == 65535 && min_of<std::int16_t> == -32768);
static_assert(max_of<std::int32_t> == 2147483647 && min_of<std::int32_t> == -2147483647 - 1);
static_assert(max_of<std::uint32_t> == 4294967295u);
static_assert(max_of<std::int64_t> == 9223372036854775807 && min_of<std::int64_t> == -9223372036854775807 - 1);
static_assert(max_of<std::uint64_t> == 18446744073709551615u);
static_assert(max_of<long long> == max_of<long> && min_of<long long> == min_of<long>);
static_assert(max_of<unsigned long long> == max_of<unsigned long>);
static_assert(bits_of<std::int8_t> == 8 && bits_of<std::uint16_t> == 16 && bits_of<int> == 32);
static_assert(bits_of<std::uint64_t> == 64 && bits_of<float> == 32 && bits_of<double> == 64);
static_assert(value_bits_of<std::int8_t> == 7 && value_bits_of<std::uint8_t> == 8);
static_assert(value_bits_of<std::int64_t> == 63 && value_bits_of<std::uint64_t> == 64);
static_assert(value_bits_of<float> == 24 && value_bits_of<double> == 53);
// The type of each limit is the type itself.
static_assert(std::is_same_v<decltype(max_of<std::uint16_t>), std::uint16_t const>);
static_assert(std::is_same_v<decltype(min_of<double>), double const>);
static_assert(std::is_same_v<decltype(bits_of<std::uint8_t>), int const>);

// The constants of the floats agree with the macros of the compiler.
// -Werror=float-equal refuses == of two floats, so the checks compare with
// <= and >=.  A NaN is unordered with each value, itself too.
template <class T>
[[nodiscard]] consteval bool same_value(T left, T right) {
    return left <= right && left >= right;
}
template <class T>
[[nodiscard]] consteval bool is_unordered(T value) {
    return !(value <= value) && !(value >= value);
}

static_assert(__GCC_IEC_559 > 0);
static_assert(__FLT_MANT_DIG__ == value_bits_of<float> && __DBL_MANT_DIG__ == value_bits_of<double>);
static_assert(same_value(max_of<float>, __FLT_MAX__) && same_value(min_of<float>, -__FLT_MAX__));
static_assert(same_value(max_of<double>, __DBL_MAX__) && same_value(min_of<double>, -__DBL_MAX__));
static_assert(same_value(min_normal_of<float>, __FLT_MIN__) && same_value(min_normal_of<double>, __DBL_MIN__));
static_assert(same_value(min_subnormal_of<float>, __FLT_DENORM_MIN__));
static_assert(same_value(min_subnormal_of<double>, __DBL_DENORM_MIN__));
static_assert(same_value(epsilon_of<float>, __FLT_EPSILON__) && same_value(epsilon_of<double>, __DBL_EPSILON__));
static_assert(infinity_of<float> > max_of<float> && -infinity_of<double> < min_of<double>);
static_assert(same_value(infinity_of<double> + 1.0, infinity_of<double>));
static_assert(is_unordered(quiet_nan_of<float>) && is_unordered(quiet_nan_of<double>));
static_assert(is_unordered(signaling_nan_of<float>) && is_unordered(signaling_nan_of<double>));
static_assert(min_subnormal_of<double> > 0.0 && min_subnormal_of<double> / 2.0 <= 0.0);
static_assert(1.0 + epsilon_of<double> > 1.0 && 1.0 + epsilon_of<double> / 2.0 <= 1.0);
static_assert(1.0f + epsilon_of<float> > 1.0f && 1.0f + epsilon_of<float> / 2.0f <= 1.0f);

// fits_each_value.
static_assert(fits_each_value<std::uint8_t, std::int16_t> && !fits_each_value<std::uint8_t, std::int8_t>);
static_assert(fits_each_value<std::int8_t, std::int64_t> && !fits_each_value<std::int8_t, std::uint64_t>);
static_assert(fits_each_value<std::uint32_t, std::uint64_t> && !fits_each_value<std::uint64_t, std::uint32_t>);
static_assert(fits_each_value<std::uint32_t, std::int64_t> && !fits_each_value<std::uint64_t, std::int64_t>);
static_assert(fits_each_value<std::int32_t, double> && !fits_each_value<std::int64_t, double>);
static_assert(fits_each_value<std::int16_t, float> && !fits_each_value<std::int32_t, float>);
static_assert(fits_each_value<float, double> && !fits_each_value<double, float> && fits_each_value<double, double>);
static_assert(!fits_each_value<float, std::int64_t> && !fits_each_value<bool, int> && !fits_each_value<int, bool>);
static_assert(fits_each_value<long, long long> && fits_each_value<long long, long>);

}  // namespace foundation::core::detail::number_checks
