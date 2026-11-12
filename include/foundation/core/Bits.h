#pragma once

// The Scalar family: the bit operations of an unsigned value.
//
//   popcount(v)                     the number of one bits
//   countl_zero(v), countr_zero(v)  the zero bits above the highest one bit,
//                                   and the zero bits below the lowest one
//                                   bit.  For 0 each count is the width
//   countl_one(v), countr_one(v)    the same counts of the one bits
//   bit_width(v)                    the number of bits that hold v: 0 for 0
//   has_single_bit(v)               true when v is a power of two
//   bit_floor(v)                    the largest power of two that is not
//                                   more than v, and 0 for 0
//   bit_ceil(v)                     the smallest power of two that is not
//                                   less than v, or no value when T cannot
//                                   hold it
//   rotl(v, n), rotr(v, n)          the rotation by n modulo the width.  A
//                                   negative n turns the other way
//   low_mask<T>(n)                  the n low bits: 0 for n <= 0, and each
//                                   bit for n >= the width
//   byteswap(v)                     the bytes of an Integer in the opposite
//                                   sequence
//
// Each operation other than byteswap takes an UnsignedInteger, because the
// sign bit of a signed value has no meaning in a count or a rotation.  Each
// count is an int, as each count of the family, and no input gives an
// undefined result.  The zero counts use the builtins with a second
// argument, which is the result for 0, so the count of 0 has no branch.
//
// std::bit_ceil is undefined when T cannot hold the power.  bit_ceil gives
// no value in its place.  std::rotl takes the count modulo the width too, and
// rotl here gives the same result for each count, also for the minimum of
// int.

#include <foundation/core/Arithmetic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Number.h>

#include <type_traits>

namespace foundation::core {

template <UnsignedInteger T>
[[nodiscard]] constexpr int popcount(T value) noexcept {
    return __builtin_popcountg(value);
}

template <UnsignedInteger T>
[[nodiscard]] constexpr int countl_zero(T value) noexcept {
    return __builtin_clzg(value, bits_of<T>);
}

template <UnsignedInteger T>
[[nodiscard]] constexpr int countr_zero(T value) noexcept {
    return __builtin_ctzg(value, bits_of<T>);
}

template <UnsignedInteger T>
[[nodiscard]] constexpr int countl_one(T value) noexcept {
    return countl_zero(static_cast<T>(~value));
}

template <UnsignedInteger T>
[[nodiscard]] constexpr int countr_one(T value) noexcept {
    return countr_zero(static_cast<T>(~value));
}

template <UnsignedInteger T>
[[nodiscard]] constexpr int bit_width(T value) noexcept {
    return bits_of<T> - countl_zero(value);
}

// The lowest one bit of a power of two is its only one bit, so v & (v - 1)
// is 0.  The test needs no count instruction.
template <UnsignedInteger T>
[[nodiscard]] constexpr bool has_single_bit(T value) noexcept {
    return value != T{0} && static_cast<T>(value & static_cast<T>(value - T{1})) == T{0};
}

template <UnsignedInteger T>
[[nodiscard]] constexpr T bit_floor(T value) noexcept {
    if (value == T{0}) return T{0};
    return static_cast<T>(T{1} << (bit_width(value) - 1));
}

// The range test comes first: each value past the top bit has no ceiling
// that T holds.  Each other value has a ceiling, so the shift has no test.
// For 0, value - 1 is the maximum, whose width is the width of T, and the
// mask makes that shift 0.  So the ceiling of 0 and of 1 is 1 with no branch.
template <UnsignedInteger T>
[[nodiscard]] constexpr Option<T> bit_ceil(T value) noexcept {
    constexpr T top_bit = static_cast<T>(T{1} << (bits_of<T> - 1));
    if (value > top_bit) return none;
    int const shift = bit_width(static_cast<T>(value - T{1})) & (bits_of<T> - 1);
    return Option<T>::some(static_cast<T>(T{1} << shift));
}

// The step of each direction is the count modulo the width, and the step of
// the other direction is its negation modulo the width.  So a step of 0
// shifts by 0 in the two directions, and no shift is by the width.  GCC
// gives one rotate instruction for this form.
template <UnsignedInteger T>
[[nodiscard]] constexpr T rotl(T value, int count) noexcept {
    unsigned const step = detail::shift_step_of<T>(count);
    unsigned const back = (0u - step) & static_cast<unsigned>(bits_of<T> - 1);
    return static_cast<T>(static_cast<T>(value << step) | static_cast<T>(value >> back));
}

template <UnsignedInteger T>
[[nodiscard]] constexpr T rotr(T value, int count) noexcept {
    unsigned const step = detail::shift_step_of<T>(count);
    unsigned const back = (0u - step) & static_cast<unsigned>(bits_of<T> - 1);
    return static_cast<T>(static_cast<T>(value >> step) | static_cast<T>(value << back));
}

template <UnsignedInteger T>
[[nodiscard]] constexpr T low_mask(int count) noexcept {
    if (count <= 0) return T{0};
    if (count >= bits_of<T>) return max_of<T>;
    return static_cast<T>(static_cast<T>(T{1} << count) - T{1});
}

template <Integer T>
[[nodiscard]] constexpr T byteswap(T value) noexcept {
    if constexpr (sizeof(T) == 1) {
        return value;
    } else {
        using Word = std::make_unsigned_t<T>;
        Word const word = static_cast<Word>(value);
        if constexpr (sizeof(T) == 2) {
            return static_cast<T>(__builtin_bswap16(word));
        } else if constexpr (sizeof(T) == 4) {
            return static_cast<T>(__builtin_bswap32(word));
        } else {
            static_assert(sizeof(T) == 8, "byteswap: an Integer of more than 8 bytes needs its own arm");
            return static_cast<T>(__builtin_bswap64(word));
        }
    }
}

}  // namespace foundation::core
