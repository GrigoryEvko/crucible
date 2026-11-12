// The compile-time checks of foundation/core/Convert.h.

#include <foundation/core/Convert.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::convert_checks {

template <class T>
[[nodiscard]] consteval bool holds(Option<T> result, T expected) {
    return result.is_some() && static_cast<Option<T>&&>(result).value_or(T{0}) == expected;
}

// narrow of an integer.
static_assert(holds(narrow<std::uint8_t>(255), std::uint8_t{255}) && narrow<std::uint8_t>(256).is_none());
static_assert(narrow<std::uint8_t>(-1).is_none() && holds(narrow<std::int8_t>(-128), std::int8_t{-128}));
static_assert(narrow<std::int64_t>(max_of<std::uint64_t>).is_none());
static_assert(holds(narrow<std::uint64_t>(std::int8_t{5}), std::uint64_t{5}));
static_assert(std::is_same_v<decltype(narrow<std::uint16_t>(1)), Option<std::uint16_t>>);

// narrow of a float truncates toward zero, and gives no value for a NaN, an
// infinity and a value out of range.
static_assert(holds(narrow<int>(2.9), 2) && holds(narrow<int>(-2.9), -2) && holds(narrow<int>(-0.0), 0));
static_assert(holds(narrow<int>(-2147483648.9), min_of<int>) && narrow<int>(-2147483649.0).is_none());
static_assert(holds(narrow<int>(2147483647.9), max_of<int>) && narrow<int>(2147483648.0).is_none());
static_assert(narrow<int>(quiet_nan_of<double>).is_none() && narrow<int>(infinity_of<double>).is_none());
static_assert(narrow<int>(-infinity_of<double>).is_none() && narrow<unsigned>(-1.0).is_none());
static_assert(holds(narrow<unsigned>(-0.5), 0u) && holds(narrow<std::uint8_t>(255.5f), std::uint8_t{255}));
static_assert(narrow<std::int64_t>(9223372036854775808.0).is_none());
static_assert(holds(narrow<std::int64_t>(-9223372036854775808.0), min_of<std::int64_t>));
static_assert(narrow<std::uint64_t>(18446744073709551616.0).is_none());
static_assert(holds(narrow<int>(-2147483648.0f), min_of<int>) && narrow<int>(2147483648.0f).is_none());
static_assert(truncation_upper<std::int32_t, double> > 2147483647.0);
static_assert(truncation_upper<std::uint64_t, float> > 1.8e19f);

// The other conversions.
static_assert(wrapping_narrow<std::uint8_t>(300) == 44 && wrapping_narrow<std::int8_t>(255) == -1);
static_assert(saturating_narrow<std::int16_t>(std::int64_t{100000}) == max_of<std::int16_t>);
static_assert(saturating_narrow<std::int16_t>(std::int64_t{-100000}) == min_of<std::int16_t>);
static_assert(saturating_narrow<std::uint8_t>(-1) == 0 && saturating_narrow<std::uint8_t>(7) == 7);
static_assert(saturating_narrow<int>(quiet_nan_of<double>) == 0 && saturating_narrow<int>(1e30) == max_of<int>);
static_assert(saturating_narrow<int>(-infinity_of<float>) == min_of<int> && saturating_narrow<int>(-2.5) == -2);
static_assert(widen<std::uint64_t>(std::uint32_t{7}) == 7u && widen<std::int64_t>(std::uint32_t{7}) == 7);
static_assert(widen<double>(std::int32_t{-7}) < -6.5 && widen<double>(1.5f) > 1.25);

}  // namespace foundation::core::detail::convert_checks
