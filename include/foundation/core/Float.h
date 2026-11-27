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
//   aarch64, runs the instruction.  The exact form of
//   detail::fma_by_integers_ adds the integer product of the two
//   significands and the significand of c in 128 bits, keeps each lost bit in
//   a sticky bit, and rounds one time.  It is the oracle of the two forms
//   below, and the form of each constant evaluation.
//
//   Each other target, such as x86-64 and x86-64-v2, runs a form in doubles
//   under the floor crucible_fp_strict (CRUCIBLE_FP_STRICT_FLOOR, with no
//   fast math).  A float goes through detail::fma_by_double_: the exact
//   product in double, a round to odd of the sum, and a round to float.  A
//   double goes through detail::fma_by_pairs_: the exact product of Dekker,
//   a two-sum and a round to odd, inside the limits where each step is
//   exact.  For a double, a NaN, an infinity, a subnormal input and each
//   input past a limit go to the exact form.  The float form takes each
//   input on its one path: a NaN or an infinity there gives the NaN or the
//   infinity of IEEE 754, with a payload that can differ from the exact
//   form, and an infinite operand raises the invalid flag in the two-sum,
//   where the instruction raises none.  A unit with no floor runs the exact
//   form for each input, because a contraction or a fast-math flag breaks
//   the exact steps of the two forms.
//
//   Each form calls no function of libm, and it gives the bits of the
//   instruction for each input, under the round to nearest with no flush.
//   Under another mode of the register the instruction follows the mode, and
//   the forms do not, so they can give different bits there.  A NaN input or
//   a product of an infinity gives a NaN or an infinity, and the payload of a
//   NaN can differ between the instruction and the other forms.
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

// The forms of the fma for a target with no fused multiply-add, under the
// floor crucible_fp_strict.  Each step is a double operation that rounds to
// nearest, and contraction is off, so each sum and each product rounds one
// time.  The forms assume the round to nearest with no flush to zero and no
// denormals as zero, as the exact form does.
//
// Round to odd (Boldo and Melquiond, "Emulation of FMA and correctly rounded
// sums: proved algorithms using rounding to odd", IEEE Transactions on
// Computers 57(4), 2008): round to odd keeps the nearest value with an odd
// last bit, unless the value is exact.  If a format has at least p + 2 bits,
// a round to odd in that format and then a round to nearest in p bits gives
// the round to nearest of the exact value in p bits.

// A pair of doubles whose exact sum is a value: high is its round to nearest,
// and low is the error.
struct FmaPair {
    double high = 0.0;
    double low = 0.0;
};

// first + second with each bit (the two-sum of Knuth): 6 operations, exact
// for finite values whose sum does not overflow.
[[nodiscard]] constexpr FmaPair fma_two_sum_(double first, double second) noexcept {
    double const sum = first + second;
    double const second_part = sum - first;
    double const first_part = sum - second_part;
    return FmaPair{sum, (first - first_part) + (second - second_part)};
}

// first + second rounded to odd.  An inexact sum with an even last bit moves
// one unit toward the error, to its neighbor with an odd last bit.  A sum
// that is a NaN or an infinity does not move: the two-sum of an infinity
// gives a NaN error, and a move would turn the infinity into a NaN or into
// the largest finite value.  The move is integer arithmetic with no branch:
// the top bit of x | -x is set when x is not zero, the exponent field plus 1
// reaches bit 11 only for a NaN or an infinity, and the move is +1 when the
// error has the sign of the sum and -1 when it does not.
[[nodiscard]] constexpr double fma_round_sum_to_odd_(double first, double second) noexcept {
    FmaPair const sum = fma_two_sum_(first, second);
    std::uint64_t const sum_bits = __builtin_bit_cast(std::uint64_t, sum.high);
    std::uint64_t const error_bits = __builtin_bit_cast(std::uint64_t, sum.low);
    std::uint64_t const error_magnitude = error_bits << 1;
    std::uint64_t const is_inexact = (error_magnitude | (std::uint64_t{0} - error_magnitude)) >> 63;
    std::uint64_t const is_special = (((sum_bits >> 52) & 0x7FFu) + 1) >> 11;
    std::uint64_t const needs_move = is_inexact & ~is_special & ~sum_bits & 1u;
    std::uint64_t const is_opposite = (sum_bits ^ error_bits) >> 63;
    std::uint64_t const move = needs_move - 2 * (needs_move & is_opposite);
    return __builtin_bit_cast(double, sum_bits + move);
}

// The fma of float in double.  The product of two floats has at most
// 2 * 24 = 48 bits, so it is exact in the 53 bits of double, and the exponent
// of each product of two finite floats is in the normal range of double.  The
// sum then rounds to odd in double, and 53 >= 24 + 2, so the round of that
// double to float is the round to nearest of a * b + c.  A zero, a NaN and
// an infinity take the same path: the double operations give the zeros,
// infinities and NaNs of IEEE 754, the round to odd does not move them, and
// the payload of a NaN can differ from the one of the exact form.  An
// infinite operand makes the two-sum compute inf - inf, so the form raises
// the invalid flag where the instruction raises none.  The form has no
// branch: about 38 instructions in a Release build at x86-64.
[[nodiscard]] constexpr float fma_by_double_(float left, float right, float addend) noexcept {
    double const product = static_cast<double>(left) * static_cast<double>(right);
    return static_cast<float>(fma_round_sum_to_odd_(product, static_cast<double>(addend)));
}

// The split of Veltkamp: high holds the upper 26 bits of the value and low
// the rest, each exact, for a value below 2^996.  Above that the product of
// the constant 2^27 + 1 can overflow.
[[nodiscard]] constexpr FmaPair fma_split_(double value) noexcept {
    double const scaled = 0x1.0000002p+27 * value;
    double const high = scaled - (scaled - value);
    return FmaPair{high, value - high};
}

// a * b with each bit (the product of Dekker): 17 operations.  The four
// partial products of the halves are exact while the exponent of the product
// is -970 or above, where the last bit of a * b is 2^-1074 or above.
[[nodiscard]] constexpr FmaPair fma_two_product_(double left, double right) noexcept {
    double const high = left * right;
    FmaPair const first = fma_split_(left);
    FmaPair const second = fma_split_(right);
    double const low = ((first.high * second.high - high) + first.high * second.low + first.low * second.high)
                     + first.low * second.low;
    return FmaPair{high, low};
}

// The emulated fma of Boldo and Melquiond, with no test of its range:
// uh + ul = a * b, th + tl = c + uh, v = the round to odd of tl + ul, and the
// result is th + v rounded to nearest.  The glibc fma of a target with no
// fused multiply-add takes the same order.  The tests read this core on each
// side of each limit of fma_by_pairs_.  When c + uh cancels, the lemma of
// Sterbenz makes th exact, so tl is 0 and v is ul, and the result has one
// round also in the subnormal range.
[[nodiscard]] constexpr double fma_pairs_core_(double left, double right, double addend) noexcept {
    FmaPair const product = fma_two_product_(left, right);
    FmaPair const sum = fma_two_sum_(addend, product.high);
    return sum.high + fma_round_sum_to_odd_(sum.low, product.low);
}

// The limits of the exponents (the unbiased exponents of the inputs) where
// each step of fma_pairs_core_ is exact and nothing overflows:
//   each factor 995 or below      the split of Veltkamp
//   the two factors 1021 or below the product rounds below 2^1023
//   the two factors -970 or above the partial products are exact
//   the addend 1022 or below      c + uh is below 2^1024 - 2^970, so the
//                                 sum does not round to an infinity
inline constexpr int kFmaPairsFactorMost = 995;
inline constexpr int kFmaPairsProductMost = 1021;
inline constexpr int kFmaPairsProductLeast = -970;
inline constexpr int kFmaPairsAddendMost = 1022;

// The inputs of fma_by_pairs_ outside its range: a NaN or an infinity goes to
// the exact form, a zero takes the expressions of the exact form, so the
// zeros keep the signs of IEEE 754, and each other input (a subnormal input,
// or an exponent past a limit) goes to the exact form.
[[nodiscard, gnu::cold, gnu::noinline]] constexpr double fma_pairs_edge_(double left, double right,
                                                                         double addend) noexcept {
    std::uint64_t const left_bits = __builtin_bit_cast(std::uint64_t, left);
    std::uint64_t const right_bits = __builtin_bit_cast(std::uint64_t, right);
    std::uint64_t const addend_bits = __builtin_bit_cast(std::uint64_t, addend);
    constexpr std::uint64_t exponent_mask = 0x7FF0000000000000u;
    bool const has_special = (left_bits & exponent_mask) == exponent_mask
                          || (right_bits & exponent_mask) == exponent_mask
                          || (addend_bits & exponent_mask) == exponent_mask;
    if (has_special) return fma_by_integers_(left, right, addend);
    if ((left_bits << 1) == 0 || (right_bits << 1) == 0) return left * right + addend;
    if ((addend_bits << 1) == 0) return left * right;
    return fma_by_integers_(left, right, addend);
}

// The fma of double in pairs of doubles.  One test reads the biased exponent
// of each input: a factor in [1, 1023 + 995], an addend in [1, 1023 + 1022]
// and a product exponent in [-970, 1021].  The field 0 of a zero or of a
// subnormal value and the field 2047 of a NaN or an infinity are outside, so
// the limits read only normal inputs.  A subnormal input has an exponent
// below the one of its field.  Each input outside goes to fma_pairs_edge_.
// About 110 instructions inside the range in a Release build at x86-64, of
// which 33 are double operations, with the compares that -fharden-compares
// of the tree checks two times.
[[nodiscard]] constexpr double fma_by_pairs_(double left, double right, double addend) noexcept {
    auto const left_biased = static_cast<std::uint32_t>(__builtin_bit_cast(std::uint64_t, left) >> 52) & 0x7FFu;
    auto const right_biased = static_cast<std::uint32_t>(__builtin_bit_cast(std::uint64_t, right) >> 52) & 0x7FFu;
    auto const addend_biased = static_cast<std::uint32_t>(__builtin_bit_cast(std::uint64_t, addend) >> 52) & 0x7FFu;
    constexpr std::uint32_t factor_span = 1023 + kFmaPairsFactorMost;
    constexpr std::uint32_t addend_span = 1023 + kFmaPairsAddendMost;
    constexpr std::uint32_t product_span = kFmaPairsProductMost - kFmaPairsProductLeast;
    // The biased exponent of the product less the lowest one: in
    // [0, product_span] inside the limits, and above it as an unsigned value
    // outside.
    std::uint32_t const product_offset = left_biased + right_biased - (2 * 1023 + kFmaPairsProductLeast);
    bool const is_inside = (left_biased - 1u < factor_span) & (right_biased - 1u < factor_span)
                         & (addend_biased - 1u < addend_span) & (product_offset <= product_span);
    if (!is_inside) [[unlikely]]
        return fma_pairs_edge_(left, right, addend);
    return fma_pairs_core_(left, right, addend);
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
#elif defined(CRUCIBLE_FP_STRICT_FLOOR) && !defined(__FAST_MATH__)
        if constexpr (std::same_as<T, float>) {
            return detail::fma_by_double_(left, right, addend);
        } else {
            return detail::fma_by_pairs_(left, right, addend);
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
