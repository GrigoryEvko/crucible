// Tests of fp::fma of foundation/core/Float.h (THE FMA): a * b + c with one
// round to nearest even on each target.
//
// The oracle is the exact sum of test/exact_sum.h: the exact product and the
// addend in a fixed-point integer, rounded one time.  The test compares the
// bits of fp::fma of this unit with the oracle, with fp::fma of the unit of
// x86-64-v3, which runs the instruction, and with fp::fma of the unit of
// x86-64, which runs the exact form in integers.  So each build compares the
// exact form and the instruction with the oracle, also a build at x86-64-v3
// such as the build of the preset default.  The cases are the hard cases of a fused
// multiply-add: exact ties and near ties of the round, the error of a product
// as the result of a cancellation, products that underflow into the
// subnormal range or to zero, sums near the largest value, the zeros of the
// two signs, the infinities and the NaNs, and random values from a fixed
// Philox seed.

#include <foundation/core/Float.h>
#include <foundation/os/X86Level.h>

#include "../exact_sum.h"
#include "../test_assert.h"
#include "fma_units.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::crucible::test::exact_fma;
using ::crucible::test::ExactLayout;
using ::crucible::test::PhiloxStream;

// The count of the random cases of each kind.
constexpr std::size_t kCasesOfEachKind = 20000;

template <class T>
using Word = typename ExactLayout<T>::Word;

template <class T>
[[nodiscard]] Word<T> bits_of_value(T value) noexcept {
    return ::fixy::bit_cast<Word<T>>(value);
}

// The two results agree when they have the same bits, or when the two are
// NaNs: the payload of a NaN is not part of THE FMA.
template <class T>
[[nodiscard]] bool agree(T left, T right) noexcept {
    if (::fixy::fp::is_nan(left) || ::fixy::fp::is_nan(right)) {
        return ::fixy::fp::is_nan(left) && ::fixy::fp::is_nan(right);
    }
    return bits_of_value(left) == bits_of_value(right);
}

// The counts of the cases, for the pass line.
struct Counts {
    std::size_t cases = 0;
    std::size_t fused = 0;
    std::size_t exact = 0;
};

// True when the host runs the code of the wide unit: x86-64-v3 on x86_64,
// and each aarch64 host.
[[nodiscard]] bool can_run_wide_unit() noexcept {
#if defined(__x86_64__)
    namespace os = ::foundation::os;
    return os::x86_level_of(os::read_x86_feature_words()) >= os::X86Level::v3;
#else
    return true;
#endif
}

// Compares fp::fma of this unit, of the wide unit and of the baseline unit
// with the oracle.
template <class T>
void check_case(T left, T right, T addend, Counts& counts) noexcept {
    T const expected = exact_fma(left, right, addend);
    T const computed = ::foundation::core::fp::fma(left, right, addend);
    if (!agree(computed, expected)) {
        ::fixy::report(::fixy::Sink::Err, "fma({}, {}, {}): the unit gives {}, the oracle gives {}\n",
                       bits_of_value(left), bits_of_value(right), bits_of_value(addend), bits_of_value(computed),
                       bits_of_value(expected));
    }
    assert(agree(computed, expected));
    ++counts.cases;
    if (::crucible::test::is_wide_unit_fused() && can_run_wide_unit()) {
        T const fused = ::crucible::test::fma_of_wide_unit(left, right, addend);
        assert(agree(fused, expected));
        ++counts.fused;
    }
    if (::crucible::test::is_baseline_unit_exact()) {
        T const exact = ::crucible::test::fma_of_baseline_unit(left, right, addend);
        if (!agree(exact, expected)) {
            ::fixy::report(::fixy::Sink::Err, "fma({}, {}, {}): the exact form gives {}, the oracle gives {}\n",
                           bits_of_value(left), bits_of_value(right), bits_of_value(addend), bits_of_value(exact),
                           bits_of_value(expected));
        }
        assert(agree(exact, expected));
        ++counts.exact;
    }
}

// A value of the bits sign, biased exponent and fraction.
template <class T>
[[nodiscard]] T value_of(bool is_negative, std::uint64_t biased, std::uint64_t fraction) noexcept {
    constexpr int fraction_bits = ExactLayout<T>::precision - 1;
    constexpr int sign_shift = ExactLayout<T>::precision + ExactLayout<T>::exponent_bits - 1;
    std::uint64_t const mask = (std::uint64_t{1} << fraction_bits) - 1;
    std::uint64_t const bits =
        (std::uint64_t{is_negative ? 1u : 0u} << sign_shift) | (biased << fraction_bits) | (fraction & mask);
    return ::fixy::bit_cast<T>(static_cast<Word<T>>(bits));
}

// The bias of the exponent of T.
template <class T>
inline constexpr std::uint64_t kBias = (std::uint64_t{1} << (ExactLayout<T>::exponent_bits - 1)) - 1;

// A finite value with a random sign and fraction, and a biased exponent of
// bias + center + a draw in [-spread, spread], kept inside the range of the
// finite values.
template <class T>
[[nodiscard]] T draw_value(PhiloxStream& stream, std::int64_t center, std::uint64_t spread) noexcept {
    constexpr auto highest = static_cast<std::int64_t>((std::uint64_t{1} << ExactLayout<T>::exponent_bits) - 2);
    std::uint64_t const bits = stream.next_wide();
    std::int64_t biased = static_cast<std::int64_t>(kBias<T>) + center
                        + static_cast<std::int64_t>(stream.below(2 * spread + 1)) - static_cast<std::int64_t>(spread);
    if (biased < 0) biased = 0;
    if (biased > highest) biased = highest;
    return value_of<T>((bits >> 63) != 0, static_cast<std::uint64_t>(biased), stream.next_wide());
}

// A value with a significand of at most bits significant bits, so a product
// of two of them is exact in the precision of T.
template <class T>
[[nodiscard]] T draw_short_value(PhiloxStream& stream, int significant_bits, std::int64_t center) noexcept {
    constexpr int fraction_bits = ExactLayout<T>::precision - 1;
    std::uint64_t const fraction = stream.next_wide() & ((std::uint64_t{1} << (significant_bits - 1)) - 1);
    std::uint64_t const biased = kBias<T> + static_cast<std::uint64_t>(center) + stream.below(8);
    return value_of<T>((stream.next_wide() & 1u) != 0, biased, fraction << (fraction_bits - (significant_bits - 1)));
}

// The unit in the last place of a finite normal value: the value of its last
// bit, with the sign of the value.
template <class T>
[[nodiscard]] T last_place_of(T value) noexcept {
    constexpr int fraction_bits = ExactLayout<T>::precision - 1;
    Word<T> const bits = bits_of_value(value);
    Word<T> const sign_and_exponent = static_cast<Word<T>>(bits >> fraction_bits);
    Word<T> const biased =
        static_cast<Word<T>>(sign_and_exponent & ((Word<T>{1} << ExactLayout<T>::exponent_bits) - 1));
    bool const is_negative = (sign_and_exponent >> ExactLayout<T>::exponent_bits) != 0;
    if (biased == 0) return value_of<T>(is_negative, 0, 1);
    if (biased <= Word<T>{fraction_bits}) return value_of<T>(is_negative, 0, std::uint64_t{1} << (biased - 1));
    return value_of<T>(is_negative, biased - static_cast<Word<T>>(fraction_bits), 0);
}

// The hard cases with a fixed form: the ties, the cancellations, the
// underflows, the overflows and the special values.
template <class T>
void test_fixed_cases(Counts& counts) noexcept {
    T const one = T{1};
    T const unit = ::fixy::epsilon_of<T>;
    T const half_unit = unit / T{2};
    T const tiny = ::fixy::min_subnormal_of<T>;
    T const smallest_normal = ::fixy::min_normal_of<T>;
    T const largest = ::fixy::max_of<T>;
    T const infinity = ::fixy::infinity_of<T>;
    T const nan = ::fixy::quiet_nan_of<T>;
    T const zero = T{0};
    // The ties of the round: 1 + u/2 is half way between 1 and 1 + u, and
    // 1 + 3u/2 is half way between 1 + u and 1 + 2u.
    check_case(one, one, half_unit, counts);
    check_case(one, one, -half_unit / T{2}, counts);
    check_case(one + unit, one, half_unit, counts);
    check_case(one + unit, one, -half_unit, counts);
    check_case(one, one + unit, half_unit + half_unit * unit, counts);
    check_case(one, one + unit, half_unit - half_unit * unit, counts);
    check_case(one + unit, one - unit, -one, counts);
    check_case(one + unit, one + unit, -one, counts);
    check_case(one + unit, one + unit, -one - unit - unit, counts);
    // The underflows: products in the subnormal range, at the bound of the
    // normal range, and below half of the smallest subnormal.
    check_case(tiny, one / T{2}, zero, counts);
    check_case(tiny, one / T{2}, tiny, counts);
    check_case(tiny, T{1.5}, -tiny, counts);
    check_case(smallest_normal, one - unit, tiny, counts);
    check_case(smallest_normal, one - unit / T{2}, -tiny, counts);
    check_case(smallest_normal, unit, -tiny, counts);
    check_case(-smallest_normal, unit, tiny, counts);
    check_case(tiny, tiny, zero, counts);
    check_case(tiny, -tiny, zero, counts);
    check_case(tiny, tiny, -zero, counts);
    // The overflows: an exact sum back inside the range, and past it.
    check_case(largest, T{2}, -largest, counts);
    check_case(largest, one + unit, -largest * unit, counts);
    check_case(largest, one, largest * unit / T{4}, counts);
    check_case(largest, one, largest * unit / T{2}, counts);
    check_case(-largest, T{2}, largest, counts);
    check_case(largest, T{2}, -infinity, counts);
    // The zeros of the two signs.
    ::fixy::FixedArray<T, 2> const firsts{zero, -zero};
    ::fixy::FixedArray<T, 3> const seconds{one, -one, zero};
    ::fixy::FixedArray<T, 3> const thirds{zero, -zero, one};
    for (T const first : firsts) {
        for (T const second : seconds) {
            for (T const third : thirds) {
                check_case(first, second, third, counts);
            }
        }
    }
    check_case(one, one, -one, counts);
    check_case(-one, one, one, counts);
    // A double case that only the sticky bit of the exact form rounds right.
    // The product (1 + 2^-31 + 2^-52)(1 + 2^-22) has a run of 20 zeros above
    // its low bits, and the addend -(2^-74 + 2^-126) cancels those low bits
    // and loses its last bit in the move.  The exact sum is 2^-126 below a
    // tie, so it rounds down, and with no sticky bit it would be a tie that
    // rounds to the even neighbor above.
    if constexpr (sizeof(T) == sizeof(double)) {
        T const first = T{1} + T{0x1p-31} + T{0x1p-52};
        T const second = T{1} + T{0x1p-22};
        T const third = -(T{0x1p-74} + T{0x1p-126});
        check_case(first, second, third, counts);
        check_case(-first, second, -third, counts);
        check_case(first, -second, -third, counts);
        assert(bits_of_value(::foundation::core::fp::fma(first, second, third))
               == bits_of_value(T{1} + T{0x1p-22} + T{0x1p-31} + T{0x1p-52}));
    }
    // The infinities and the NaNs.
    check_case(infinity, zero, one, counts);
    check_case(infinity, one, -infinity, counts);
    check_case(infinity, -one, -infinity, counts);
    check_case(one, one, infinity, counts);
    check_case(nan, one, one, counts);
    check_case(one, nan, infinity, counts);
    check_case(one, one, nan, counts);
}

// The random cases of each kind.
template <class T>
void test_random_cases(std::uint64_t seed, Counts& counts) noexcept {
    constexpr auto precision = static_cast<std::int64_t>(ExactLayout<T>::precision);
    constexpr auto bias = static_cast<std::int64_t>(kBias<T>);
    PhiloxStream stream{seed};
    for (std::size_t index = 0; index < kCasesOfEachKind; ++index) {
        // Values of a moderate range.
        T const left = draw_value<T>(stream, 0, 20);
        T const right = draw_value<T>(stream, 0, 20);
        check_case(left, right, draw_value<T>(stream, 0, 45), counts);
        // The error of the product: c is minus the rounded product, so the
        // result is the exact error, and with a small change it is a near
        // tie of the next bits.
        T const product = left * right;
        check_case(left, right, -product, counts);
        check_case(left, right, -product + last_place_of(product) / T{2}, counts);
        // A product of two short values is exact, so an addend of half of its
        // last place gives an exact tie, and a tiny change of the addend
        // decides it.
        T const short_left = draw_short_value<T>(stream, static_cast<int>(precision / 2), 0);
        T const short_right = draw_short_value<T>(stream, static_cast<int>(precision / 2), 0);
        T const exact = short_left * short_right;
        T const tie = last_place_of(exact) / T{2};
        check_case(short_left, short_right, tie, counts);
        check_case(short_left, short_right, -tie, counts);
        check_case(short_left, short_right, tie + tie * ::fixy::epsilon_of<T>, counts);
        check_case(short_left, short_right, tie - tie * ::fixy::epsilon_of<T> / T{2}, counts);
        // Products near the subnormal range and near the largest value.
        T const low_left = draw_value<T>(stream, -bias / 2, 8);
        T const low_right = draw_value<T>(stream, -bias / 2 - precision / 2, precision);
        check_case(low_left, low_right, draw_value<T>(stream, -bias - precision / 2, precision), counts);
        check_case(low_left, low_right, -(low_left * low_right), counts);
        T const high_left = draw_value<T>(stream, bias / 2, 2);
        T const high_right = draw_value<T>(stream, bias / 2, 2);
        check_case(high_left, high_right, draw_value<T>(stream, bias, 2), counts);
        // A wide spread of the exponents of the three values.
        check_case(draw_value<T>(stream, 0, static_cast<std::uint64_t>(bias)),
                   draw_value<T>(stream, 0, static_cast<std::uint64_t>(bias)),
                   draw_value<T>(stream, 0, static_cast<std::uint64_t>(bias)), counts);
    }
}

}  // namespace

int main() {
    Counts float_counts{};
    Counts double_counts{};
    test_fixed_cases<float>(float_counts);
    test_fixed_cases<double>(double_counts);
    test_random_cases<float>(0xF3A0000000000001u, float_counts);
    test_random_cases<double>(0xF3A0000000000002u, double_counts);
    ::crucible::test::pass(
        "test_core_fma: {} float cases and {} double cases agree with the exact oracle, {} and {} "
        "with the instruction of the wide unit, {} and {} with the exact form of the baseline unit\n",
        float_counts.cases, double_counts.cases, float_counts.fused, double_counts.fused, float_counts.exact,
        double_counts.exact);
    return 0;
}
