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
//   fp::canonicalize_nan(v)        v, or the canonical NaN quiet_nan_of<T>
//                                  when v is a NaN
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
// The NaN that an arithmetic operation gives has bits that change with the
// host and with the order of the operands.  x86 keeps the payload of one
// operand and gives 0xFFC00000 for inf + -inf, aarch64 gives its default NaN
// 0x7FC00000, and GCC can swap the operands of an add.  canonicalize_nan
// gives one NaN on each host: 0x7FC00000 for a float, 0x7FF8000000000000 for
// a double.  Its test reads the bits as an integer, so it raises no flag,
// also for a signaling NaN.
//
// The hardware square root of a negative value or of a NaN gives a NaN
// whose bits change with the target and with the input: x86_64 gives a NaN
// with the sign bit for a negative value.  The library sqrt also writes
// errno, so GCC adds a call of it.  fp::sqrt does a quiet test of v >= 0
// first, so each target gives quiet_nan_of<T> for those inputs, a quiet
// NaN raises no flag, and GCC removes the call.  sqrt(-0) is -0, as IEEE
// 754 states.
//
// fma
//   fma gives a * b + c with one round to nearest even, on each target.  A
//   target with a fused multiply-add, such as x86-64-v3, x86-64-v4 and
//   aarch64, runs the instruction.  Each other target, such as x86-64 and
//   x86-64-v2, runs the exact form of detail::fma_by_integers_: it adds the
//   integer product of the two significands and the significand of c in 128
//   bits, keeps each lost bit in a sticky bit, and rounds one time.  The form
//   calls no function of libm, and it gives the bits of the instruction for
//   each input, under the round to nearest with no flush.  Under another mode
//   of the register the instruction follows the mode, and the exact form does
//   not, so the two forms can give different bits there.  A NaN input or a
//   product of an infinity gives a NaN or an infinity, and the payload of a
//   NaN can differ between the two forms.
//
//   The body reads the target, so fma is in the inline namespace
//   CRUCIBLE_SIMD_TARGET (the target namespace in foundation/Platform.h).  A
//   constant evaluation runs the exact form.  The exact form is an
//   out-of-line function, and its parts compile to different instructions
//   at each level, such as LZCNT for __builtin_clzll at x86-64-v3.  So the
//   exact form and its parts are in the same inline namespace of detail.  A
//   unit of one level then calls only the body of its own level, also in a
//   binary that links units of two levels, as the tests of the products do.
//
// Each operation is constexpr.  A constant evaluation of GCC 16 gives no
// result that is an infinity for sqrt: sqrt(+inf) is not a constant
// expression.  A run gives the IEEE result for it.
//
// total_key orders each value as the total order: each negative NaN, -inf,
// the negative values, -0, +0, the positive values, +inf, and each positive
// NaN.  A negative value gets each bit flipped, and each other value gets
// its sign bit set: one shift, one or and one xor.

#include <foundation/Platform.h>
#include <foundation/core/Number.h>

#include <concepts>
#include <cstdint>
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

// A NaN has its bits with no sign above the bits of an infinity.  One and,
// one compare and one select.
template <Float T>
[[nodiscard]] constexpr T canonicalize_nan(T value) noexcept {
    using Bits = detail::FloatBits<T>;
    using Word = typename Bits::Word;
    Word const magnitude = static_cast<Word>(__builtin_bit_cast(Word, value) & static_cast<Word>(~Word{0} >> 1));
    return magnitude > Bits::infinity ? quiet_nan_of<T> : value;
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

}  // namespace foundation::core::fp

namespace foundation::core::detail {

// The exact form of the fma and its parts, in the namespace of the target.
inline namespace CRUCIBLE_SIMD_TARGET {

__extension__ using FmaWord = unsigned __int128;

// The bit length of a word above zero: one more than the position of its
// highest set bit.
[[nodiscard]] constexpr int fma_bit_length_(FmaWord value) noexcept {
    auto const high = static_cast<std::uint64_t>(value >> 64);
    if (high != 0) return 128 - __builtin_clzll(high);
    return 64 - __builtin_clzll(static_cast<std::uint64_t>(value));
}

// The layout of the bits of a Float: the bits of the fraction, the bias of
// the exponent, the largest biased exponent of a finite value, and the
// exponent of the last bit of a subnormal value.
template <Float T>
struct FmaLayout {
    static constexpr int fraction_bits = FloatBits<T>::significand_bits - 1;
    static constexpr int exponent_bits = bits_of<T> - 1 - fraction_bits;
    static constexpr int bias = (1 << (exponent_bits - 1)) - 1;
    static constexpr int infinite_exponent = (1 << exponent_bits) - 1;
    static constexpr int lowest_exponent = 1 - bias - fraction_bits;
};

// True for +0 and -0.  The test reads the bits with no sign, so it needs no
// compare of floats.
template <Float T>
[[nodiscard]] constexpr bool fma_is_zero_(T value) noexcept {
    using Word = typename FloatBits<T>::Word;
    return static_cast<Word>(__builtin_bit_cast(Word, value) << 1) == Word{0};
}

// A finite value that is not zero, as (-1)^is_negative * significand *
// 2^exponent, with an integer significand below 2^significand_bits.
struct FmaTerm {
    FmaWord significand = 0;
    int exponent = 0;
    bool is_negative = false;
};

template <Float T>
[[nodiscard]] constexpr FmaTerm fma_term_(T value) noexcept {
    using Layout = FmaLayout<T>;
    using Word = typename FloatBits<T>::Word;
    Word const bits = __builtin_bit_cast(Word, value);
    Word const fraction = static_cast<Word>(bits & ((Word{1} << Layout::fraction_bits) - 1));
    auto const biased = static_cast<int>((bits >> Layout::fraction_bits) & Layout::infinite_exponent);
    bool const is_negative = (bits >> (bits_of<T> - 1)) != 0;
    if (biased == 0) return FmaTerm{fraction, Layout::lowest_exponent, is_negative};
    return FmaTerm{FmaWord{fraction} | (FmaWord{1} << Layout::fraction_bits),
                   biased - Layout::bias - Layout::fraction_bits, is_negative};
}

// The value of the integer kept times 2^exponent with the sign, for a kept
// below 2^significand_bits.  A kept of a full significand is a normal value,
// and an exponent past the largest finite value gives an infinity.  A kept
// below that has the exponent of a subnormal value.
template <Float T>
[[nodiscard]] constexpr T fma_value_(FmaWord kept, int exponent, bool is_negative) noexcept {
    using Layout = FmaLayout<T>;
    using Word = typename FloatBits<T>::Word;
    Word const sign = is_negative ? static_cast<Word>(Word{1} << (bits_of<T> - 1)) : Word{0};
    if (kept < (FmaWord{1} << Layout::fraction_bits)) {
        return __builtin_bit_cast(T, static_cast<Word>(sign | static_cast<Word>(kept)));
    }
    int const biased = exponent + Layout::bias + Layout::fraction_bits;
    if (biased >= Layout::infinite_exponent)
        return __builtin_bit_cast(T, static_cast<Word>(sign | FloatBits<T>::infinity));
    Word const fraction = static_cast<Word>(kept - (FmaWord{1} << Layout::fraction_bits));
    return __builtin_bit_cast(
        T, static_cast<Word>(sign | (static_cast<Word>(biased) << Layout::fraction_bits) | fraction));
}

// The exact form of the fma: a * b + c with one round to nearest even, in
// integers.  Each significand moves to the bit 125 of a 128-bit word.  The
// smaller term moves right to the exponent of the larger term, and an or into
// its bit 0 (the sticky bit) keeps each bit that the move drops.  A move drops
// a bit only past the lowest set bit of the term, so the sum then has its
// highest bit at bit 124 or above, and the bit of the round is 70 bits or more
// above bit 0.  The sticky bit then decides only an exact tie of the kept
// bits: when the dropped bits of a term that subtracts make the exact sum a
// little less than the tie, the sticky bit makes the bits below the round bit
// less than a half, and the sum rounds down and not to even.  About 40 integer
// operations.  The form is a function of its own, so a target with no fused
// multiply-add calls it at each fma.
template <Float T>
[[nodiscard, gnu::noinline]] constexpr T fma_by_integers_(T left, T right, T addend) noexcept {
    using Layout = FmaLayout<T>;
    constexpr int top = 126;
    constexpr int precision = FloatBits<T>::significand_bits;
    if (!__builtin_isfinite(left) || !__builtin_isfinite(right)) return left * right + addend;
    if (!__builtin_isfinite(addend)) return addend;
    // A zero product is exact, and the add of a zero is exact, so each sign
    // of a zero follows the add of IEEE 754.
    if (fma_is_zero_(left) || fma_is_zero_(right)) return left * right + addend;
    if (fma_is_zero_(addend)) return left * right;
    FmaTerm const first = fma_term_(left);
    FmaTerm const second = fma_term_(right);
    FmaTerm const third = fma_term_(addend);
    FmaWord const product = first.significand * second.significand;
    int const product_move = top - fma_bit_length_(product);
    int const addend_move = top - fma_bit_length_(third.significand);
    FmaTerm high{product << product_move, first.exponent + second.exponent - product_move,
                 first.is_negative != second.is_negative};
    FmaTerm low{third.significand << addend_move, third.exponent - addend_move, third.is_negative};
    if (low.exponent > high.exponent) {
        FmaTerm const larger = low;
        low = high;
        high = larger;
    }
    int const gap = high.exponent - low.exponent;
    if (gap >= top) {
        low.significand = 1;
    } else if (gap > 0) {
        FmaWord const lost = low.significand & ((FmaWord{1} << gap) - 1);
        low.significand = (low.significand >> gap) | FmaWord{lost != 0 ? 1u : 0u};
    }
    FmaWord sum = 0;
    bool is_negative = high.is_negative;
    if (high.is_negative == low.is_negative) {
        sum = high.significand + low.significand;
    } else if (high.significand >= low.significand) {
        sum = high.significand - low.significand;
    } else {
        sum = low.significand - high.significand;
        is_negative = low.is_negative;
    }
    if (sum == 0) return T{0};
    int const highest = fma_bit_length_(sum) - 1;
    int shift = highest - (precision - 1);
    if (high.exponent + shift < Layout::lowest_exponent) shift = Layout::lowest_exponent - high.exponent;
    FmaWord kept = 0;
    if (shift <= 0) {
        kept = sum << -shift;
    } else if (shift < 128) {
        FmaWord const rest = sum & ((FmaWord{1} << shift) - 1);
        FmaWord const half = FmaWord{1} << (shift - 1);
        kept = sum >> shift;
        if (rest > half || (rest == half && (kept & 1u) != 0)) ++kept;
    }
    int exponent = high.exponent + shift;
    if ((kept >> precision) != 0) {
        kept >>= 1;
        ++exponent;
    }
    return fma_value_<T>(kept, exponent, is_negative);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace foundation::core::detail

namespace foundation::core::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

// a * b + c with one round (the fma).
template <Float T>
[[nodiscard]] constexpr T fma(T left, T right, T addend) noexcept {
    if consteval {
        return detail::fma_by_integers_(left, right, addend);
    } else {
#if defined(__FMA__) || defined(__aarch64__)
        if constexpr (std::same_as<T, float>) {
            return __builtin_fmaf(left, right, addend);
        } else {
            return __builtin_fma(left, right, addend);
        }
#else
        return detail::fma_by_integers_(left, right, addend);
#endif
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

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
