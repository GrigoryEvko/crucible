// The correct round of log(double) of fixy/fp: the hard inputs, a sample
// against a digest of CORE-MATH, and the slow path on its own.
//
// Each expected value comes from cr_log of the pinned CORE-MATH.  The test
// checks three things:
//   - Each input of fp_log_hard_cases.h gives its correctly rounded result,
//     through log and through the slow path detail::log_accurate_.
//   - The sample, four regions of 2^14 inputs, gives the digest of cr_log on
//     the same inputs.  The regions: the bit patterns of the positive finite
//     inputs, the bit patterns of the subnormal inputs, the values of the path
//     near 1, and the values from sqrt2/2 to sqrt2, where e = 0.
//   - The slow path on its own gives the same digest: each input of the
//     sample takes the slow path.  So the slow path is correct on the inputs
//     that the fast path and the path near 1 answer too.
//
// The proof of the correct round is the error budget and the coverage table
// of fixy/fp/Log.h, which `utils/tools/fp_tables.py log` prints:
//   normal inputs     Lefevre and Muller 2001, property 2, margin 19.16 bits,
//                     and the list log.wc of CORE-MATH, margin 22.14 bits
//   subnormal inputs  the BaCSeL search at -m 48 of log.wc, with its own
//                     sentence "we should have all worst cases with 47
//                     identical bits or more in this file", and the counts of
//                     its subnormal binades as the positive control.  The open
//                     confirmation is an own pinned BaCSeL run on the
//                     subnormal binades at -m 48.
// The sweep `bench_fp_sweep hard log` runs each input of log.wc against
// CORE-MATH and MPFR, through log and the slow path, and `bench_fp_sweep
// subnormal log` takes 2^28 subnormal inputs through log and the slow path
// against MPFR.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/fp/Log.h>
#include <foundation/effects/Effect.h>

#include "fp_log_hard_cases.h"

#include <cstdint>

namespace {

namespace fh = ::test_fp_log;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The digest of cr_log of the pinned CORE-MATH on the sample: FNV-1a of 64
// bits over the bits of each result, in the order of the sample.
constexpr std::uint64_t kCoreMathDigest = 0xD20DB66BB9D441BFULL;

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
    Region{true, 0.0, 0.0, 1, 0x7FF0000000000000ULL - 1},
    Region{true, 0.0, 0.0, 1, 0x0010000000000000ULL - 1},
    Region{false, 1.0 - 0x1p-9, 0x1p-8, 0, 0},
    Region{false, 0x1.6a09e667f3bccp-1, 0x1.6a09e667f3bcdp0 - 0x1.6a09e667f3bccp-1, 0, 0},
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

void test_the_hard_inputs() {
    for (fh::HardCase const& item : fh::kLogHardCases) {
        double const input = opaque_double(item.input);
        std::uint64_t const got = bits_of(::fixy::fp::log(input));
        if (got != item.expected) {
            ::fixy::report(::fixy::Sink::Err,
                           "test_fp_log_double: log of 0x{} gives 0x{}, the expected bits are 0x{}\n",
                           ::fixy::Hex<16>{item.input}, ::fixy::Hex<16>{got}, ::fixy::Hex<16>{item.expected});
        }
        CRUCIBLE_TEST_ASSERT(kTest, got == item.expected);
        std::uint64_t const slow = bits_of(::fixy::fp::detail::log_accurate_(input));
        if (slow != item.expected) {
            ::fixy::report(::fixy::Sink::Err,
                           "test_fp_log_double: the slow path of 0x{} gives 0x{}, the expected bits are 0x{}\n",
                           ::fixy::Hex<16>{item.input}, ::fixy::Hex<16>{slow}, ::fixy::Hex<16>{item.expected});
        }
        CRUCIBLE_TEST_ASSERT(kTest, slow == item.expected);
    }
    ::fixy::check::pass(kTest, "test_fp_log_double: {} hard inputs are correctly rounded\n", fh::kLogHardCases.size());
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
    std::uint64_t const digest = digest_of([](double value) noexcept { return ::fixy::fp::log(value); });
    std::uint64_t const slow_digest =
        digest_of([](double value) noexcept { return ::fixy::fp::detail::log_accurate_(value); });
    if (digest != kCoreMathDigest || slow_digest != kCoreMathDigest) {
        ::fixy::report(::fixy::Sink::Err,
                       "test_fp_log_double: the digests of log and of the slow path are 0x{} and 0x{}, the digest "
                       "of CORE-MATH is 0x{}\n",
                       ::fixy::Hex<16>{digest}, ::fixy::Hex<16>{slow_digest}, ::fixy::Hex<16>{kCoreMathDigest});
    }
    CRUCIBLE_TEST_ASSERT(kTest, digest == kCoreMathDigest);
    CRUCIBLE_TEST_ASSERT(kTest, slow_digest == kCoreMathDigest);
    ::fixy::check::pass(kTest, "test_fp_log_double: log and the slow path give the digest of CORE-MATH\n");
}

}  // namespace

int main() {
    test_the_hard_inputs();
    test_the_sample_gives_the_digest_of_core_math();
    return 0;
}
