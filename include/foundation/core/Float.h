#pragma once

// The Scalar family: the operations of a floating value, in namespace fp.
//
//   fp::is_nan(v), fp::is_inf(v)   true for a NaN, and for each infinity
//   fp::is_finite(v)               true for each value that is not a NaN and
//                                  not an infinity
//   fp::is_normal(v)               true for a finite value that is not 0 and
//                                  not subnormal
//   fp::sign_bit(v)                true when the sign bit is set, also for -0
//                                  and for a NaN with the sign bit
//   fp::abs(v)                     v with the sign bit clear
//   fp::copy_sign(v, sign)         v with the sign bit of sign
//   fp::sqrt(v)                    the square root, and the canonical quiet
//                                  NaN for a negative value or a NaN
//   fp::fma(a, b, c)               a * b + c with one rounding
//   fp::total_key(v)               the unsigned key of the total order of
//                                  IEEE 754
//
// Each operation takes a Float, float or double, so a promotion of float to
// double never changes a signaling NaN.  No operation reads or writes
// errno, a locale or a global.  The classes, sign_bit, abs and copy_sign
// read only the bits, so a NaN keeps its payload, as IEEE 754 states for
// abs and copySign.
//
// The hardware square root of a negative value or of a NaN gives a NaN
// whose bits change with the target and with the input: x86_64 gives a NaN
// with the sign bit for a negative value.  The library sqrt also writes
// errno, so GCC adds a call of it.  fp::sqrt does a quiet test of v >= 0
// first, so each target gives quiet_nan_of<T> for those inputs, a quiet
// NaN raises no flag, and GCC removes the call.  sqrt(-0) is -0, as IEEE
// 754 states.
//
// fma is one instruction on a target with a fused multiply-add, such as
// x86-64-v3 and aarch64.  For -march=x86-64 it is a call of fma of libm,
// which gives the same result with one rounding.
//
// Each operation is constexpr.  A constant evaluation of GCC 16 gives no
// result that is an infinity for sqrt and fma: sqrt(+inf), and an fma that
// overflows, are not constant expressions.  A run gives the IEEE result for
// them.
//
// total_key orders each value as the total order: each negative NaN, -inf,
// the negative values, -0, +0, the positive values, +inf, and each positive
// NaN.  A negative value gets each bit flipped, and each other value gets
// its sign bit set: one shift, one or and one xor.

#include <foundation/core/Number.h>

#include <concepts>
#include <type_traits>

namespace foundation::core::fp {

template <Float T>
[[nodiscard]] constexpr bool is_nan(T value) noexcept {
    return __builtin_isnan(value) != 0;
}

template <Float T>
[[nodiscard]] constexpr bool is_inf(T value) noexcept {
    return __builtin_isinf(value) != 0;
}

template <Float T>
[[nodiscard]] constexpr bool is_finite(T value) noexcept {
    return __builtin_isfinite(value) != 0;
}

template <Float T>
[[nodiscard]] constexpr bool is_normal(T value) noexcept {
    return __builtin_isnormal(value) != 0;
}

template <Float T>
[[nodiscard]] constexpr bool sign_bit(T value) noexcept {
    return __builtin_signbit(value) != 0;
}

template <Float T>
[[nodiscard]] constexpr T abs(T value) noexcept {
    if constexpr (std::same_as<T, float>) {
        return __builtin_fabsf(value);
    } else {
        return __builtin_fabs(value);
    }
}

template <Float T>
[[nodiscard]] constexpr T copy_sign(T value, T sign) noexcept {
    if constexpr (std::same_as<T, float>) {
        return __builtin_copysignf(value, sign);
    } else {
        return __builtin_copysign(value, sign);
    }
}

// The quiet compare raises no flag for a quiet NaN, where v >= 0 raises the
// invalid flag for each NaN.
template <Float T>
[[nodiscard]] constexpr T sqrt(T value) noexcept {
    if (!__builtin_isgreaterequal(value, T{0})) return quiet_nan_of<T>;
    if constexpr (std::same_as<T, float>) {
        return __builtin_sqrtf(value);
    } else {
        return __builtin_sqrt(value);
    }
}

template <Float T>
[[nodiscard]] constexpr T fma(T left, T right, T addend) noexcept {
    if constexpr (std::same_as<T, float>) {
        return __builtin_fmaf(left, right, addend);
    } else {
        return __builtin_fma(left, right, addend);
    }
}

template <Float T>
[[nodiscard]] constexpr typename detail::FloatBits<T>::Word total_key(T value) noexcept {
    using Word = typename detail::FloatBits<T>::Word;
    using Signed = std::make_signed_t<Word>;
    Word const bits = __builtin_bit_cast(Word, value);
    Word const spread = static_cast<Word>(static_cast<Signed>(bits) >> (bits_of<T> - 1));
    Word const sign = static_cast<Word>(Word{1} << (bits_of<T> - 1));
    return static_cast<Word>(bits ^ static_cast<Word>(spread | sign));
}

}  // namespace foundation::core::fp
