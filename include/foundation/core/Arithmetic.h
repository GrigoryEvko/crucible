#pragma once

// The Scalar family: arithmetic on an Integer that names the result of an
// overflow at the call site.
//
//   checked_add, checked_sub, checked_mul   the exact result, or no value
//   checked_div, checked_rem                no value for a zero divisor, and
//                                           the quotient of the minimum by
//                                           -1 has no value
//   checked_neg                             the exact negation, or no value
//   checked_shl(v, n), checked_shr(v, n)    no value for a count outside
//                                           [0, bits), and checked_shl has
//                                           no value when v * 2^n does not
//                                           fit
//   wrapping_add, wrapping_sub, wrapping_mul, wrapping_neg
//                                           the result modulo 2^bits
//   wrapping_shl(v, n), wrapping_shr(v, n)  the count modulo bits
//   saturating_add, saturating_sub, saturating_mul
//                                           the exact result, or the
//                                           nearest limit of the type
//   gcd(a, b)                               the greatest common divisor as
//                                           an unsigned value, for each input
//   widening_mul(a, b)                      the full product of two unsigned
//                                           values, as two halves
//
// Each operation takes two operands of one Integer type, so a pair of
// different types does not compile and no promotion hides a conversion.
// No result is undefined for an input.  The modular forms use the overflow
// builtins, so no result changes with -fno-strict-overflow.  A checked form
// costs one test of the flag of the builtin.  The trapping form of an
// operation is its checked form and Option::expect with a reason.
//
// A count of a shift is an int, as each count of the family.  In C++20 a
// shift of a negative signed value is defined, and so is the conversion of
// an unsigned value to a signed type: both are modular.  So the right shift
// of a negative value is the floor of the quotient by 2^n.
//
// The remainder has the sign of the dividend: -7 rem 3 is -1.  The minimum
// rem -1 is 0, although the quotient of the same operands has no value.
//
// gcd(0, 0) is 0, and gcd(min_of<std::int64_t>, 0) is 2^63, which the
// unsigned result holds.  The loop is the binary algorithm: at most one
// step for each bit of the operands.
//
// WideProduct holds the low half first.  The x86-64 System V ABI gives the
// first half back in rax and the second in rdx, the two registers of the
// result of mul, so the return moves no register.

#include <foundation/core/Choice.h>
#include <foundation/core/Number.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail {

__extension__ using WideWord = unsigned __int128;

// The magnitude of a value as an unsigned value of the same width, for each
// value: the minimum of a signed type is 2^(bits-1).
template <Integer T>
[[nodiscard]] constexpr std::make_unsigned_t<T> magnitude_of(T value) noexcept {
    using Bits = std::make_unsigned_t<T>;
    if constexpr (std::is_signed_v<T>) {
        return value < T{0} ? static_cast<Bits>(Bits{0} - static_cast<Bits>(value)) : static_cast<Bits>(value);
    } else {
        return value;
    }
}

// The count of a shift modulo the width of T.  The width is a power of two,
// so the modulo is one AND, also for a negative count.
template <Integer T>
[[nodiscard]] constexpr unsigned shift_step_of(int count) noexcept {
    return static_cast<unsigned>(count) & static_cast<unsigned>(bits_of<T> - 1);
}

}  // namespace detail

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_add(T left, T right) noexcept {
    T sum{};
    if (__builtin_add_overflow(left, right, &sum)) return none;
    return Option<T>::some(sum);
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_sub(T left, T right) noexcept {
    T difference{};
    if (__builtin_sub_overflow(left, right, &difference)) return none;
    return Option<T>::some(difference);
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_mul(T left, T right) noexcept {
    T product{};
    if (__builtin_mul_overflow(left, right, &product)) return none;
    return Option<T>::some(product);
}

namespace detail {

// True for the quotient of the minimum by -1, which a signed type does not
// hold.
template <Integer T>
[[nodiscard]] constexpr bool is_minimum_by_minus_one(T dividend, T divisor) noexcept {
    if constexpr (std::is_signed_v<T>) {
        return dividend == min_of<T> && divisor == T{-1};
    } else {
        return false;
    }
}

}  // namespace detail

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_div(T dividend, T divisor) noexcept {
    if (divisor == T{0} || detail::is_minimum_by_minus_one(dividend, divisor)) return none;
    return Option<T>::some(static_cast<T>(dividend / divisor));
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_rem(T dividend, T divisor) noexcept {
    if (divisor == T{0}) return none;
    if constexpr (std::is_signed_v<T>) {
        if (divisor == T{-1}) return Option<T>::some(T{0});
    }
    return Option<T>::some(static_cast<T>(dividend % divisor));
}

// An unsigned value has a negation only for 0.
template <Integer T>
[[nodiscard]] constexpr Option<T> checked_neg(T value) noexcept {
    T negated{};
    if (__builtin_sub_overflow(T{0}, value, &negated)) return none;
    return Option<T>::some(negated);
}

// The right shift gives back the value only when the left shift lost no
// bit and kept the sign.
template <Integer T>
[[nodiscard]] constexpr Option<T> checked_shl(T value, int count) noexcept {
    if (count < 0 || count >= bits_of<T>) return none;
    using Bits = std::make_unsigned_t<T>;
    T const shifted = static_cast<T>(static_cast<Bits>(static_cast<Bits>(value) << count));
    if (static_cast<T>(shifted >> count) != value) return none;
    return Option<T>::some(shifted);
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_shr(T value, int count) noexcept {
    if (count < 0 || count >= bits_of<T>) return none;
    return Option<T>::some(static_cast<T>(value >> count));
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_add(T left, T right) noexcept {
    T sum{};
    static_cast<void>(__builtin_add_overflow(left, right, &sum));
    return sum;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_sub(T left, T right) noexcept {
    T difference{};
    static_cast<void>(__builtin_sub_overflow(left, right, &difference));
    return difference;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_mul(T left, T right) noexcept {
    T product{};
    static_cast<void>(__builtin_mul_overflow(left, right, &product));
    return product;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_neg(T value) noexcept {
    T negated{};
    static_cast<void>(__builtin_sub_overflow(T{0}, value, &negated));
    return negated;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_shl(T value, int count) noexcept {
    using Bits = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<Bits>(static_cast<Bits>(value) << detail::shift_step_of<T>(count)));
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_shr(T value, int count) noexcept {
    return static_cast<T>(value >> detail::shift_step_of<T>(count));
}

// The exact result is the likely path.  With no hint, GCC 16 puts the
// clamp on the fall-through path, and the exact result costs two taken
// jumps.  An overflow of a sum goes the way of the sign of the second
// operand.
template <Integer T>
[[nodiscard]] constexpr T saturating_add(T left, T right) noexcept {
    T sum{};
    if (!__builtin_add_overflow(left, right, &sum)) [[likely]] {
        return sum;
    }
    if constexpr (std::is_signed_v<T>) {
        return right < T{0} ? min_of<T> : max_of<T>;
    } else {
        return max_of<T>;
    }
}

template <Integer T>
[[nodiscard]] constexpr T saturating_sub(T left, T right) noexcept {
    T difference{};
    if (!__builtin_sub_overflow(left, right, &difference)) [[likely]] {
        return difference;
    }
    if constexpr (std::is_signed_v<T>) {
        return right < T{0} ? max_of<T> : min_of<T>;
    } else {
        return T{0};
    }
}

template <Integer T>
[[nodiscard]] constexpr T saturating_mul(T left, T right) noexcept {
    T product{};
    if (!__builtin_mul_overflow(left, right, &product)) [[likely]] {
        return product;
    }
    if constexpr (std::is_signed_v<T>) {
        return (left < T{0}) != (right < T{0}) ? min_of<T> : max_of<T>;
    } else {
        return max_of<T>;
    }
}

template <Integer T>
[[nodiscard]] constexpr std::make_unsigned_t<T> gcd(T left, T right) noexcept {
    using Bits = std::make_unsigned_t<T>;
    Bits first = detail::magnitude_of(left);
    Bits second = detail::magnitude_of(right);
    if (first == 0) return second;
    if (second == 0) return first;
    int const shared = __builtin_ctzg(static_cast<Bits>(first | second));
    first = static_cast<Bits>(first >> __builtin_ctzg(first));
    do {
        second = static_cast<Bits>(second >> __builtin_ctzg(second));
        if (first > second) {
            Bits const smaller = second;
            second = first;
            first = smaller;
        }
        second = static_cast<Bits>(second - first);
    } while (second != 0);
    return static_cast<Bits>(first << shared);
}

// The full product of two unsigned values: the low half and the high half.
template <UnsignedInteger T>
struct WideProduct final {
    T low = 0;
    T high = 0;
};

template <UnsignedInteger T>
[[nodiscard]] constexpr WideProduct<T> widening_mul(T left, T right) noexcept {
    if constexpr (sizeof(T) == sizeof(std::uint64_t)) {
        detail::WideWord const product = static_cast<detail::WideWord>(left) * static_cast<detail::WideWord>(right);
        return WideProduct<T>{static_cast<T>(product), static_cast<T>(product >> 64)};
    } else {
        std::uint64_t const product = std::uint64_t{left} * std::uint64_t{right};
        return WideProduct<T>{static_cast<T>(product), static_cast<T>(product >> bits_of<T>)};
    }
}

}  // namespace foundation::core
