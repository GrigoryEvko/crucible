#pragma once

// The Scalar family: the gates of a number, and the limits of each number
// type.
//
//   Integer<T>             an integral type that is not cv-qualified, not
//                          bool and not a character type
//   SignedInteger<T>       an Integer with a sign
//   UnsignedInteger<T>     an Integer with no sign
//   Float<T>               float or double
//   Arithmetic<T>          an Integer or a Float
//   fits_each_value<F, T>  each value of F is a value of T
//
//   max_of<T>, min_of<T>   the highest and the lowest value
//   bits_of<T>             the bits of the object
//   value_bits_of<T>       the bits that hold the value: the digits of an
//                          Integer, the significand of a Float with its
//                          hidden bit
//   min_normal_of<T>, min_subnormal_of<T>, epsilon_of<T>, infinity_of<T>,
//   quiet_nan_of<T>, signaling_nan_of<T>   the constants of a Float
//
// A character type is not an Integer.  Plain char is signed on x86_64 and
// unsigned on aarch64, so its limits change with the target.  A text is
// not a number, and the Region family holds the text types.  signed char
// and unsigned char are std::int8_t and std::uint8_t, so they stay.
//
// long double is not a Float.  It has 80 value bits in 16 bytes on x86_64
// and 128 value bits on aarch64, so its results change with the target.
//
// Each limit is a constant of its type.  A type that is not Arithmetic does
// not compile, where std::numeric_limits gives zeros.  min_of of a Float is
// the lowest value, the negative of max_of.  The smallest positive normal
// value is min_normal_of.  The constants of a Float come from their IEEE
// 754 bits, so each target has the same bits.  The quiet NaN is the
// positive canonical NaN, and the signaling NaN has the second bit of the
// significand set.
//
// fits_each_value tells when a conversion keeps each value.  An Integer
// fits an Integer of the same sign with as many value bits or more, and an
// unsigned Integer fits a signed Integer with more value bits.  A signed
// Integer never fits an unsigned one.  An Integer fits a Float when the
// significand holds each of its value bits, and float fits double.  A
// Float never fits an Integer.

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace foundation::core {

// A gate reads only concepts and consteval functions that are not
// templates, because a unit can specialize a template and change what the
// gate admits.
template <class T>
concept Integer = std::integral<T> && std::same_as<T, std::remove_cv_t<T>> && !std::same_as<T, bool>
               && !std::same_as<T, char> && !std::same_as<T, wchar_t> && !std::same_as<T, char8_t>
               && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

template <class T>
concept SignedInteger = Integer<T> && std::is_signed_v<T>;

template <class T>
concept UnsignedInteger = Integer<T> && std::is_unsigned_v<T>;

template <class T>
concept Float = std::same_as<T, float> || std::same_as<T, double>;

template <class T>
concept Arithmetic = Integer<T> || Float<T>;

namespace detail {

// The IEEE 754 bits of the constants of a Float, and the word of its bits.
template <class T>
struct FloatBits;

template <>
struct FloatBits<float> final {
    using Word = std::uint32_t;
    static constexpr Word highest = 0x7F7FFFFFu;
    static constexpr Word lowest = 0xFF7FFFFFu;
    static constexpr Word min_normal = 0x00800000u;
    static constexpr Word min_subnormal = 0x00000001u;
    static constexpr Word epsilon = 0x34000000u;
    static constexpr Word infinity = 0x7F800000u;
    static constexpr Word quiet_nan = 0x7FC00000u;
    static constexpr Word signaling_nan = 0x7FA00000u;
    static constexpr int significand_bits = 24;
};

template <>
struct FloatBits<double> final {
    using Word = std::uint64_t;
    static constexpr Word highest = 0x7FEFFFFFFFFFFFFFu;
    static constexpr Word lowest = 0xFFEFFFFFFFFFFFFFu;
    static constexpr Word min_normal = 0x0010000000000000u;
    static constexpr Word min_subnormal = 0x0000000000000001u;
    static constexpr Word epsilon = 0x3CB0000000000000u;
    static constexpr Word infinity = 0x7FF0000000000000u;
    static constexpr Word quiet_nan = 0x7FF8000000000000u;
    static constexpr Word signaling_nan = 0x7FF4000000000000u;
    static constexpr int significand_bits = 53;
};

template <class T>
[[nodiscard]] consteval T max_for() noexcept {
    if constexpr (Float<T>) {
        return __builtin_bit_cast(T, FloatBits<T>::highest);
    } else if constexpr (std::is_unsigned_v<T>) {
        return static_cast<T>(~T{0});
    } else {
        using Bits = std::make_unsigned_t<T>;
        return static_cast<T>(static_cast<Bits>(static_cast<Bits>(~Bits{0}) >> 1));
    }
}

template <class T>
[[nodiscard]] consteval T min_for() noexcept {
    if constexpr (Float<T>) {
        return __builtin_bit_cast(T, FloatBits<T>::lowest);
    } else if constexpr (std::is_unsigned_v<T>) {
        return T{0};
    } else {
        return static_cast<T>(-max_for<T>() - 1);
    }
}

// The value bits of an Arithmetic type: the digits of an integer, and the
// significand of a float with its hidden bit.  The caller makes sure that
// the type is Arithmetic.
[[nodiscard]] consteval int value_bits_in(std::meta::info type) {
    if (type == ^^float) return FloatBits<float>::significand_bits;
    if (type == ^^double) return FloatBits<double>::significand_bits;
    return static_cast<int>(std::meta::size_of(type)) * __CHAR_BIT__ - (std::meta::is_signed_type(type) ? 1 : 0);
}

// True when each value of the Arithmetic type from is a value of the
// Arithmetic type to.
[[nodiscard]] consteval bool fits_each_value_in(std::meta::info from, std::meta::info to) {
    bool const is_float_from = from == ^^float || from == ^^double;
    bool const is_float_to = to == ^^float || to == ^^double;
    if (is_float_from && !is_float_to) return false;
    if (!is_float_from && !is_float_to && std::meta::is_signed_type(from) && !std::meta::is_signed_type(to)) {
        return false;
    }
    return value_bits_in(from) <= value_bits_in(to);
}

}  // namespace detail

template <class From, class To>
concept fits_each_value = Arithmetic<From> && Arithmetic<To> && detail::fits_each_value_in(^^From, ^^To);

template <Arithmetic T>
inline constexpr T max_of = detail::max_for<T>();

template <Arithmetic T>
inline constexpr T min_of = detail::min_for<T>();

template <Arithmetic T>
inline constexpr int bits_of = static_cast<int>(sizeof(T)) * __CHAR_BIT__;

template <Arithmetic T>
inline constexpr int value_bits_of = detail::value_bits_in(^^T);

template <Float T>
inline constexpr T min_normal_of = __builtin_bit_cast(T, detail::FloatBits<T>::min_normal);

template <Float T>
inline constexpr T min_subnormal_of = __builtin_bit_cast(T, detail::FloatBits<T>::min_subnormal);

template <Float T>
inline constexpr T epsilon_of = __builtin_bit_cast(T, detail::FloatBits<T>::epsilon);

template <Float T>
inline constexpr T infinity_of = __builtin_bit_cast(T, detail::FloatBits<T>::infinity);

template <Float T>
inline constexpr T quiet_nan_of = __builtin_bit_cast(T, detail::FloatBits<T>::quiet_nan);

template <Float T>
inline constexpr T signaling_nan_of = __builtin_bit_cast(T, detail::FloatBits<T>::signaling_nan);

}  // namespace foundation::core
