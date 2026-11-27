#pragma once

// Arithmetic on pairs of doubles for the slow paths of fixy/fp, with the same
// bits on each target and each level of the instruction set.
//
//   DoubleWord                  high + low, an unevaluated sum of two doubles
//   two_sum_(a, b)              a + b with each bit, 6 operations
//   fast_two_sum_(a, b)         a + b with each bit for an exponent of a at
//                               least the exponent of b, 3 operations
//   two_product_(a, b)          a * b with each bit, through one fma
//   dw_plus_fp_, dw_plus_dw_,   a pair plus a double or a pair, and a pair
//   dw_times_fp_, dw_times_dw_  times a double or a pair, each with a stated
//                               relative error
//   round_triple_(h, m, l)      the round to nearest of h + m + l with each
//                               bit of the three
//
// Notation: u = 2^-53, and RN is the round to nearest with ties to even.  A
// pair is normal when high = RN(high + low).  Each bound holds for finite
// values with no overflow, and with no result below 2^-969, so the error of
// each round is a double, as the round of an error of a product needs.  Each
// operation is an add, a subtract or a multiply of IEEE 754, or a fused
// multiply-add of detail::fma_of_finite (fixy/fp/FmaOfFinite.h), which rounds
// one time on each target.  So each level gives the same bits.  The callers
// send no NaN.
//
// The operations on pairs are the algorithms of Joldes, Muller and Popescu,
// "Tight and rigorous error bounds for basic building blocks of double-word
// arithmetic" (ACM Transactions on Mathematical Software 44(2), 2017).  Each
// input pair is normal, and so is each result pair.  Each bound is relative
// to the exact result of the operation on the input values.
//
// test/fixy/test_fp_double_word.cpp checks each operation: the exact
// operations against a second method, and the bound of each operation on
// pairs against the exact result in an expansion of doubles.

#include <foundation/Platform.h>
#include <fixy/fp/FmaOfFinite.h>

#include <cstdint>

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "fixy/fp/DoubleWord.h needs the floor crucible_fp_strict (-ffp-contract=off). Without it the compiler can fuse a multiply and an add, and the bits change with the level."
#endif

namespace fixy::fp::detail {

// A value as the unevaluated sum high + low of two doubles.
struct DoubleWord {
    double high = 0.0;
    double low = 0.0;
};

inline namespace CRUCIBLE_SIMD_TARGET {

// first + second with each bit: the two-sum of Knuth.  high is
// RN(first + second), and high + low is first + second exactly, for finite
// values whose sum does not overflow.  6 operations.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord two_sum_(double first, double second) noexcept {
    double const sum = first + second;
    double const second_part = sum - first;
    double const first_part = sum - second_part;
    return DoubleWord{sum, (first - first_part) + (second - second_part)};
}

// first + second with each bit: the fast two-sum of Dekker.  The result is
// exact when the exponent of first is at least the exponent of second, for
// example when |first| >= |second| or first is 0.  For other inputs the low
// part can be wrong.  3 operations.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord fast_two_sum_(double first, double second) noexcept {
    double const sum = first + second;
    return DoubleWord{sum, second - (sum - first)};
}

// left * right with each bit: high is RN(left * right), and high + low is the
// product exactly, when the product does not overflow and its error is not
// below 2^-1074.  One multiply and one fma.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord two_product_(double left, double right) noexcept {
    double const product = left * right;
    return DoubleWord{product, ::fixy::fp::detail::fma_of_finite(left, right, -product)};
}

// A pair plus a double, algorithm 4 (DWPlusFP): a relative error of at most
// 2u^2.  10 operations.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord dw_plus_fp_(DoubleWord value, double addend) noexcept {
    DoubleWord const sum = two_sum_(value.high, addend);
    return fast_two_sum_(sum.high, value.low + sum.low);
}

// A pair plus a pair, algorithm 6 (AccurateDWPlusDW): a relative error of at
// most 3u^2 / (1 - 4u).  20 operations.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord dw_plus_dw_(DoubleWord first, DoubleWord second) noexcept {
    DoubleWord const high = two_sum_(first.high, second.high);
    DoubleWord const low = two_sum_(first.low, second.low);
    DoubleWord const middle = fast_two_sum_(high.high, high.low + low.high);
    return fast_two_sum_(middle.high, low.low + middle.low);
}

// A pair times a double, algorithm 9 (DWTimesFP3): a relative error of at most
// 2u^2.  2 fma and 4 other operations.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord dw_times_fp_(DoubleWord value, double factor) noexcept {
    DoubleWord const product = two_product_(value.high, factor);
    return fast_two_sum_(product.high, ::fixy::fp::detail::fma_of_finite(value.low, factor, product.low));
}

// A pair times a pair, algorithm 12 (DWTimesDW3): a relative error of at most
// 5u^2.  3 fma and 6 other operations.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord dw_times_dw_(DoubleWord first, DoubleWord second) noexcept {
    DoubleWord const product = two_product_(first.high, second.high);
    double const low_product = first.low * second.low;
    double const cross = ::fixy::fp::detail::fma_of_finite(first.high, second.low, low_product);
    double const cross_sum = ::fixy::fp::detail::fma_of_finite(first.low, second.high, cross);
    return fast_two_sum_(product.high, product.low + cross_sum);
}

// The round to nearest of high + middle + low with each bit, for high > 0 and
// below the largest finite value.  middle is at most a little more than half
// the gap from high to a neighbor, as the error of the round of high + middle
// with a small part added, and middle = RN(middle + low).  Then the sum rounds
// as high + middle, unless middle is half the gap to a neighbor of high,
// exactly.  There the sign of low decides, and a low of 0 is a tie that
// rounds to even.  The gaps above and below high differ at a power of two.
[[nodiscard]] constexpr double round_triple_(double high, double middle, double low) noexcept {
    std::uint64_t const high_bits = __builtin_bit_cast(std::uint64_t, high);
    double const above = __builtin_bit_cast(double, high_bits + 1);
    double const below = __builtin_bit_cast(double, high_bits - 1);
    std::uint64_t const middle_bits = __builtin_bit_cast(std::uint64_t, middle);
    if (middle_bits == __builtin_bit_cast(std::uint64_t, (above - high) * 0.5)) {
        if (low > 0.0) return above;
        if (low < 0.0) return high;
    } else if (middle_bits == __builtin_bit_cast(std::uint64_t, (below - high) * 0.5)) {
        if (low < 0.0) return below;
        if (low > 0.0) return high;
    }
    return high + middle;
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail
