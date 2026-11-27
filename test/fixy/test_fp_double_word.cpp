// The operations of fixy/fp/DoubleWord.h at run time.
//
//   - two_sum_ gives RN(a + b) and the error that fast_two_sum_ gives with the
//     larger magnitude first, the condition of the theorem of Dekker.
//   - two_product_ gives the product of Dekker of foundation/core/Float.h,
//     a second method with no fma.
//   - Each operation on pairs keeps its bound: the test holds the exact
//     result in an expansion of doubles, the sum of parts with no overlap that
//     the two-sum builds (the growth of Shewchuk), and it measures the error
//     of the pair against it.
//   - round_triple_ gives the round to nearest at each tie: above and below
//     1, where the two gaps differ, and inside a binade.
//
// The sample walks the bits of the mantissa with a Weyl step and takes an
// exponent from -30 to 30 and a sign from other bits of the step.  Each pair
// of the sample is normal: its high part is the round of its sum.  A part of
// the sums takes two values of opposite sign and close magnitude, so the
// adds also cancel.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/fp/DoubleWord.h>
#include <foundation/core/Float.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace fd = ::fixy::fp::detail;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The count of the inputs of each part of the test.
constexpr std::uint64_t kSampleCount = std::uint64_t{1} << 16;

constexpr double kUnit = 0x1p-53;

// Reading through a volatile keeps the optimizer from folding a call into a
// constant, so each call runs the code of the build.
[[nodiscard]] double opaque_double(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink = bits;
    return ::fixy::bit_cast<double>(std::uint64_t{sink});
}

[[nodiscard]] std::uint64_t bits_of(double value) noexcept { return ::fixy::bit_cast<std::uint64_t>(value); }

[[nodiscard]] bool is_zero(double value) noexcept { return (bits_of(value) << 1) == 0; }

[[nodiscard]] double magnitude_of(double value) noexcept { return value < 0.0 ? -value : value; }

// The double of a step of the walk: the mantissa of the step, an exponent from
// -30 to 30 and a sign.
[[nodiscard]] double sample_double(std::uint64_t index, std::uint64_t stream) noexcept {
    std::uint64_t const step = (index + 1) * 0x9E3779B97F4A7C15ULL + stream * 0xD1B54A32D192ED03ULL;
    std::uint64_t const mantissa = step & 0x000FFFFFFFFFFFFFULL;
    std::uint64_t const exponent = 1023 - 30 + (step >> 52) % 61;
    std::uint64_t const sign = (step >> 51) & 1;
    return opaque_double((sign << 63) | (exponent << 52) | mantissa);
}

// A normal pair near a double of the walk: the low part is a fraction of the
// unit in the last place of the high part.
[[nodiscard]] fd::DoubleWord sample_pair(std::uint64_t index, std::uint64_t stream) noexcept {
    double const high = sample_double(index, stream);
    double const fraction = sample_double(index, stream + 7) * 0x1p-30;
    return fd::fast_two_sum_(high, high * kUnit * fraction);
}

// The exact value of a sum of doubles, as parts in the order of increasing
// magnitude with no overlap.  An add of a double is the growth of Shewchuk:
// a two-sum with each part, from the smallest, keeps each error as a part.
constexpr std::size_t kMostParts = 24;

struct Expansion {
    ::fixy::FixedArray<double, kMostParts> parts{};
    std::size_t count = 0;
};

[[nodiscard]] ::fixy::Index<kMostParts> index_of(std::size_t position) noexcept {
    return ::fixy::Index<kMostParts>::check(position).expect("an expansion of the test holds at most 24 parts");
}

void grow(Expansion& expansion, double value) noexcept {
    double carry = value;
    for (std::size_t position = 0; position < expansion.count; ++position) {
        fd::DoubleWord const sum = fd::two_sum_(carry, expansion.parts.at(index_of(position)));
        expansion.parts.set(index_of(position), sum.low);
        carry = sum.high;
    }
    expansion.parts.set(index_of(expansion.count), carry);
    ++expansion.count;
}

// The value of an expansion in one double: the sum from the smallest part,
// with a relative error of a few units.
[[nodiscard]] double value_of(Expansion const& expansion) noexcept {
    double total = 0.0;
    for (std::size_t position = 0; position < expansion.count; ++position)
        total += expansion.parts.at(index_of(position));
    return total;
}

void grow_product(Expansion& expansion, double left, double right) noexcept {
    fd::DoubleWord const product = fd::two_product_(left, right);
    grow(expansion, product.high);
    grow(expansion, product.low);
}

// The relative error of a result pair against an exact value, which must be at
// most the bound with a small margin for the value of the expansions.
[[nodiscard]] bool is_within(Expansion exact, fd::DoubleWord result, double bound) noexcept {
    double const reference = value_of(exact);
    grow(exact, -result.high);
    grow(exact, -result.low);
    double const error = value_of(exact);
    if (is_zero(reference)) return is_zero(error);
    return magnitude_of(error) <= bound * magnitude_of(reference) * (1.0 + 0x1p-40);
}

void test_the_exact_sums_and_products() {
    for (std::uint64_t index = 0; index < kSampleCount; ++index) {
        double const first = sample_double(index, 0);
        double const second = sample_double(index, 1);
        fd::DoubleWord const sum = fd::two_sum_(first, second);
        bool const is_first_larger = magnitude_of(first) >= magnitude_of(second);
        fd::DoubleWord const ordered =
            is_first_larger ? fd::fast_two_sum_(first, second) : fd::fast_two_sum_(second, first);
        CRUCIBLE_TEST_ASSERT(kTest, bits_of(sum.high) == bits_of(first + second));
        CRUCIBLE_TEST_ASSERT(kTest, bits_of(sum.high) == bits_of(ordered.high));
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of(sum.low) == bits_of(ordered.low) || (is_zero(sum.low) && is_zero(ordered.low)));
        fd::DoubleWord const product = fd::two_product_(first, second);
        ::foundation::core::detail::FmaPair const dekker = ::foundation::core::detail::fma_two_product_(first, second);
        CRUCIBLE_TEST_ASSERT(kTest, bits_of(product.high) == bits_of(dekker.high));
        CRUCIBLE_TEST_ASSERT(kTest, bits_of(product.low) == bits_of(dekker.low)
                                        || (is_zero(product.low) && is_zero(dekker.low)));
    }
    ::fixy::check::pass(kTest, "test_fp_double_word: two_sum_ and two_product_ agree with a second method\n");
}

void test_the_bounds_of_the_operations_on_pairs() {
    double const add_pair_bound = 3.0 * kUnit * kUnit / (1.0 - 4.0 * kUnit);
    for (std::uint64_t index = 0; index < kSampleCount; ++index) {
        fd::DoubleWord const first = sample_pair(index, 2);
        fd::DoubleWord const near = sample_pair(index, 3);
        // One input in four cancels: the second pair is close to -first.
        fd::DoubleWord const second =
            (index & 3) == 0 ? fd::fast_two_sum_(-first.high, -first.high * near.high * 0x1p-60) : near;
        double const factor = sample_double(index, 4);

        Expansion plus_fp{};
        grow(plus_fp, first.high);
        grow(plus_fp, first.low);
        grow(plus_fp, factor);
        CRUCIBLE_TEST_ASSERT(kTest, is_within(plus_fp, fd::dw_plus_fp_(first, factor), 2.0 * kUnit * kUnit));

        Expansion plus_dw{};
        grow(plus_dw, first.high);
        grow(plus_dw, first.low);
        grow(plus_dw, second.high);
        grow(plus_dw, second.low);
        CRUCIBLE_TEST_ASSERT(kTest, is_within(plus_dw, fd::dw_plus_dw_(first, second), add_pair_bound));

        Expansion times_fp{};
        grow_product(times_fp, first.high, factor);
        grow_product(times_fp, first.low, factor);
        CRUCIBLE_TEST_ASSERT(kTest, is_within(times_fp, fd::dw_times_fp_(first, factor), 2.0 * kUnit * kUnit));

        Expansion times_dw{};
        grow_product(times_dw, first.high, second.high);
        grow_product(times_dw, first.high, second.low);
        grow_product(times_dw, first.low, second.high);
        grow_product(times_dw, first.low, second.low);
        CRUCIBLE_TEST_ASSERT(kTest, is_within(times_dw, fd::dw_times_dw_(first, second), 5.0 * kUnit * kUnit));
    }
    ::fixy::check::pass(kTest, "test_fp_double_word: each operation on pairs keeps its bound on {} inputs\n",
                        kSampleCount);
}

// One case of the round of three doubles and its expected bits.
struct TripleCase {
    double high = 0.0;
    double middle = 0.0;
    double low = 0.0;
    double expected = 0.0;
};

void test_the_round_of_three_doubles() {
    double const odd = 1.0 + 0x1p-52;
    constexpr double kTiny = 0x1p-120;
    ::fixy::FixedArray<TripleCase, 16> const cases{
        // At 1: the gap above is 2^-52, the gap below 2^-53.
        TripleCase{1.0, 0x1p-53, kTiny, 1.0 + 0x1p-52},
        TripleCase{1.0, 0x1p-53, -kTiny, 1.0},
        TripleCase{1.0, 0x1p-53, 0.0, 1.0},
        TripleCase{1.0, -0x1p-54, -kTiny, 1.0 - 0x1p-53},
        TripleCase{1.0, -0x1p-54, kTiny, 1.0},
        TripleCase{1.0, -0x1p-54, 0.0, 1.0},
        // Inside the binade of 1.5, with an even and an odd high part.
        TripleCase{1.5, 0x1p-53, kTiny, 1.5 + 0x1p-52},
        TripleCase{1.5, 0x1p-53, -kTiny, 1.5},
        TripleCase{1.5, -0x1p-53, -kTiny, 1.5 - 0x1p-52},
        TripleCase{1.5, -0x1p-53, kTiny, 1.5},
        TripleCase{odd, 0x1p-53, 0.0, 1.0 + 0x1p-51},
        TripleCase{odd, -0x1p-53, 0.0, 1.0},
        // Away from a tie the sum rounds as high + middle.
        TripleCase{1.0, 0x1p-54, -kTiny, 1.0},
        TripleCase{1.0, 0x1p-53 + 0x1p-105, -kTiny, 1.0 + 0x1p-52},
        TripleCase{1.0, 0x1p-53 - 0x1p-106, kTiny, 1.0},
        TripleCase{1.0, -0x1p-54 - 0x1p-106, kTiny, 1.0 - 0x1p-53},
    };
    for (TripleCase const& item : cases) {
        double const got = fd::round_triple_(opaque_double(bits_of(item.high)), opaque_double(bits_of(item.middle)),
                                             opaque_double(bits_of(item.low)));
        if (bits_of(got) != bits_of(item.expected)) {
            ::fixy::report(::fixy::Sink::Err, "test_fp_double_word: round_triple_({}, {}, {}) gives {}, not {}\n",
                           item.high, item.middle, item.low, got, item.expected);
        }
        CRUCIBLE_TEST_ASSERT(kTest, bits_of(got) == bits_of(item.expected));
    }
    ::fixy::check::pass(kTest, "test_fp_double_word: round_triple_ rounds each tie\n");
}

}  // namespace

int main() {
    test_the_exact_sums_and_products();
    test_the_bounds_of_the_operations_on_pairs();
    test_the_round_of_three_doubles();
    return 0;
}
