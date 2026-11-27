// The correct round of exp(double) of fixy/fp: the hard inputs, a sample
// against a digest of CORE-MATH, and the slow path on its own.
//
// Each expected value comes from cr_exp of the pinned CORE-MATH.  The test
// checks three things:
//   - Each input of fp_exp_hard_cases.h gives its correctly rounded result,
//     through exp and through the slow path detail::exp_accurate_.
//   - The sample, four regions of 2^14 inputs, gives the digest of cr_exp on
//     the same inputs.  The regions: the values from -746 to 710, the bit
//     patterns from 2^-54 to 512 of each sign, and the values from -1 to 1.
//   - The slow path on its own gives the same digest: each input of the
//     sample in its domain takes the slow path, and each other input takes
//     exp.  So the slow path is correct on the inputs that the fast path
//     answers too.
//
// The proof of the correct round is the error budget and the coverage table
// of fixy/fp/Exp.h, which `utils/tools/fp_tables.py exp` and `exp-coverage`
// print:
//   |x| < 2^-54                 analytic
//   2^-54 <= |x| < 2^-30        own exhaustive check, margin 9.59 bits
//   2^-30 <= |x|                Lefevre and Muller 2001, margin 7.19 bits,
//                               and the list exp.wc of CORE-MATH, -m 42, -m 44
//                               in [-2^-11, -2^-12), margin 8.50 bits
//   a subnormal result          the list exp.wc, -m 34, margin 19.19 bits
//   below 2^-1074, the edges    analytic
// The sweep `bench_fp_sweep hard exp` runs each of the 1129422 inputs of
// exp.wc against CORE-MATH and MPFR, through exp and the slow path.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/fp/Exp.h>
#include <foundation/effects/Effect.h>

#include "fp_exp_hard_cases.h"

#include <cstdint>

namespace {

namespace fh = ::test_fp_exp;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The digest of cr_exp of the pinned CORE-MATH on the sample: FNV-1a of 64
// bits over the bits of each result, in the order of the sample.
constexpr std::uint64_t kCoreMathDigest = 0xF383BDA031AAB8A4ULL;

// The count of the inputs of each region of the sample.
constexpr std::uint64_t kRegionCount = std::uint64_t{1} << 14;

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

// A region of the sample: values from start to start + width, or the bit
// patterns from first_bits on, bit_count of them.
struct Region {
    bool is_bits = false;
    double start = 0.0;
    double width = 0.0;
    std::uint64_t first_bits = 0;
    std::uint64_t bit_count = 0;
};

constexpr ::fixy::FixedArray<Region, 4> kRegions{
    Region{false, -746.0, 1456.0, 0, 0},
    Region{true, 0.0, 0.0, 0x3C90000000000000ULL, 0x4080000000000000ULL - 0x3C90000000000000ULL},
    Region{true, 0.0, 0.0, 0xBC90000000000000ULL, 0xC080000000000000ULL - 0xBC90000000000000ULL},
    Region{false, -1.0, 2.0, 0, 0},
};

// Reading through a volatile keeps the optimizer from folding a call into a
// constant, so each call runs the code of the build.
[[nodiscard]] double opaque_double(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink = bits;
    return ::fixy::bit_cast<double>(std::uint64_t{sink});
}

[[nodiscard]] std::uint64_t bits_of(double value) noexcept { return ::fixy::bit_cast<std::uint64_t>(value); }

// The input of a region at an index of its walk, from a Weyl step.
[[nodiscard]] double input_of(Region const& region, std::uint64_t index) noexcept {
    std::uint64_t const step = (index + 1) * 0x9E3779B97F4A7C15ULL;
    if (region.is_bits) return opaque_double(region.first_bits + step % region.bit_count);
    double const unit = static_cast<double>(step >> 11) * 0x1p-53;
    return opaque_double(bits_of(region.start + region.width * unit));
}

// The domain of the slow path: 2^-54 <= |x|, and a result from 2^-1074 up
// to the largest finite value.
[[nodiscard]] bool is_in_slow_domain(double value) noexcept {
    double const magnitude = value < 0.0 ? -value : value;
    return !(magnitude < 0x1p-54) && value > ::fixy::fp::detail::kExpZeroAtOrBelow
        && !(value > ::fixy::fp::detail::kExpLargestFinite);
}

[[nodiscard]] double slow_or_exp(double value) noexcept {
    return is_in_slow_domain(value) ? ::fixy::fp::detail::exp_accurate_(value) : ::fixy::fp::exp(value);
}

void test_the_hard_inputs() {
    for (fh::HardCase const& item : fh::kExpHardCases) {
        double const input = opaque_double(item.input);
        std::uint64_t const got = bits_of(::fixy::fp::exp(input));
        if (got != item.expected) {
            ::fixy::report(::fixy::Sink::Err,
                           "test_fp_exp_double: exp of 0x{} gives 0x{}, the expected bits are 0x{}\n",
                           ::fixy::Hex<16>{item.input}, ::fixy::Hex<16>{got}, ::fixy::Hex<16>{item.expected});
        }
        CRUCIBLE_TEST_ASSERT(kTest, got == item.expected);
        if (!is_in_slow_domain(input)) continue;
        std::uint64_t const slow = bits_of(::fixy::fp::detail::exp_accurate_(input));
        if (slow != item.expected) {
            ::fixy::report(::fixy::Sink::Err,
                           "test_fp_exp_double: the slow path of 0x{} gives 0x{}, the expected bits are 0x{}\n",
                           ::fixy::Hex<16>{item.input}, ::fixy::Hex<16>{slow}, ::fixy::Hex<16>{item.expected});
        }
        CRUCIBLE_TEST_ASSERT(kTest, slow == item.expected);
    }
    ::fixy::check::pass(kTest, "test_fp_exp_double: {} hard inputs are correctly rounded\n", fh::kExpHardCases.size());
}

// The digest of a function on the sample.  O(4 * kRegionCount).
template <class Function>
[[nodiscard]] std::uint64_t digest_of(Function const& function) noexcept {
    std::uint64_t digest = kFnvOffset;
    for (Region const& region : kRegions) {
        for (std::uint64_t index = 0; index < kRegionCount; ++index) {
            std::uint64_t const word = bits_of(function(input_of(region, index)));
            for (int byte = 0; byte < 8; ++byte)
                digest = (digest ^ ((word >> (8 * byte)) & 0xFFu)) * kFnvPrime;
        }
    }
    return digest;
}

void test_the_sample_gives_the_digest_of_core_math() {
    std::uint64_t const digest = digest_of([](double value) noexcept { return ::fixy::fp::exp(value); });
    std::uint64_t const slow_digest = digest_of([](double value) noexcept { return slow_or_exp(value); });
    if (digest != kCoreMathDigest || slow_digest != kCoreMathDigest) {
        ::fixy::report(::fixy::Sink::Err,
                       "test_fp_exp_double: the digests of exp and of the slow path are 0x{} and 0x{}, the digest "
                       "of CORE-MATH is 0x{}\n",
                       ::fixy::Hex<16>{digest}, ::fixy::Hex<16>{slow_digest}, ::fixy::Hex<16>{kCoreMathDigest});
    }
    CRUCIBLE_TEST_ASSERT(kTest, digest == kCoreMathDigest);
    CRUCIBLE_TEST_ASSERT(kTest, slow_digest == kCoreMathDigest);
    ::fixy::check::pass(kTest, "test_fp_exp_double: exp and the slow path give the digest of CORE-MATH\n");
}

}  // namespace

int main() {
    test_the_hard_inputs();
    test_the_sample_gives_the_digest_of_core_math();
    return 0;
}
