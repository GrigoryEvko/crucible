#pragma once

// The Scalar family: the conversions of a number to an integer type.
//
//   narrow<To>(v)             the value v as a To, or no value when To does
//                             not hold v.  For a Float v: no value for a NaN,
//                             an infinity or a value that truncates out of
//                             the range of To, else the truncation toward
//                             zero
//   wrapping_narrow<To>(v)    the low bits of an integer v
//   saturating_narrow<To>(v)  v clamped to the range of To, and 0 for a NaN
//   widen<To>(v)              v as a To, when each value of the type of v is
//                             a value of To.  It has no check
//
// static_cast of a float to an integer type is undefined behavior when the
// truncated value is out of the range of the type, and narrow is its total
// form.  The test of the range compares the float with two powers of two,
// which each Float holds exactly, so no call of trunc is necessary:
//
//   unsigned To with n value bits   -1 < v < 2^n
//   signed To with n value bits     -2^n - 1 < v < 2^n, or -2^n <= v < 2^n
//                                   when the significand of the Float has
//                                   at most n bits.  -2^n - 1 then has no
//                                   Float, and no Float is between it and
//                                   -2^n
//
// A NaN fails each compare, so it gives no value.  A conversion of an
// integer to a Float, and of a double to a float, is defined by IEEE 754
// for each value, so it stays a static_cast.

#include <foundation/core/Choice.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Number.h>

#include <type_traits>

namespace foundation::core {

namespace detail {

// 2^count as a Float, exact.
template <Float T>
[[nodiscard]] consteval T power_of_two(int count) noexcept {
    T power = T{1};
    for (int step = 0; step < count; ++step)
        power = power * T{2};
    return power;
}

// The exclusive upper bound of the values of From that truncate into To.
template <Integer To, Float From>
inline constexpr From truncation_upper = power_of_two<From>(value_bits_of<To>);

// True when the truncation of value is a value of To.
template <Integer To, Float From>
[[nodiscard]] constexpr bool truncates_into(From value) noexcept {
    constexpr From upper = truncation_upper<To, From>;
    if constexpr (std::is_unsigned_v<To>) {
        return value > From{-1} && value < upper;
    } else if constexpr (value_bits_of<From> > value_bits_of<To>) {
        return value > -upper - From{1} && value < upper;
    } else {
        return value >= -upper && value < upper;
    }
}

}  // namespace detail

template <Integer To, Integer From>
[[nodiscard]] constexpr Option<To> narrow(From value) noexcept {
    if (!fits<To>(value)) return none;
    return Option<To>::some(static_cast<To>(value));
}

template <Integer To, Float From>
[[nodiscard]] constexpr Option<To> narrow(From value) noexcept {
    if (!detail::truncates_into<To>(value)) return none;
    return Option<To>::some(static_cast<To>(value));
}

template <Integer To, Integer From>
[[nodiscard]] constexpr To wrapping_narrow(From value) noexcept {
    return static_cast<To>(value);
}

template <Integer To, Integer From>
[[nodiscard]] constexpr To saturating_narrow(From value) noexcept {
    if (detail::is_less(value, min_of<To>)) return min_of<To>;
    if (detail::is_less(max_of<To>, value)) return max_of<To>;
    return static_cast<To>(value);
}

template <Integer To, Float From>
[[nodiscard]] constexpr To saturating_narrow(From value) noexcept {
    if (__builtin_isnan(value)) return To{0};
    if (detail::truncates_into<To>(value)) return static_cast<To>(value);
    return value < From{0} ? min_of<To> : max_of<To>;
}

template <Arithmetic To, Arithmetic From>
    requires fits_each_value<From, To>
[[nodiscard]] constexpr To widen(From value) noexcept {
    return static_cast<To>(value);
}

}  // namespace foundation::core
