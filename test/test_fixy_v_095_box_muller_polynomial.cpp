// The polynomial form of Box-Muller exists because the library form is
// not bit-stable across platforms.  Nothing here compares the two: they
// are not meant to agree bit for bit, so a cross-check would only restate
// that they differ.  What is checked instead is that the polynomial form
// repeats exactly, stays finite, and still produces a normal distribution.

#include <crucible/Philox.h>
#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/fixy/fp/_Polynomial.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace cwrap = crucible::fixy::wrap;

namespace {

using crucible::Philox;

// The two entry points sit at different determinism tiers.  Asserting it
// here means a change that swaps them stops the build rather than quietly
// moving every sampler onto the platform-dependent path.
static_assert(decltype(Philox::box_muller_det(0u, 0u))::tier == cwrap::DetSafeTier_v::MonotonicClockRead,
              "box_muller_det must stay at the clock-read tier, the library path");

static_assert(decltype(Philox::box_muller_polynomial_det(0u, 0u))::tier == cwrap::DetSafeTier_v::PhiloxRng,
              "box_muller_polynomial_det must stay at the generator tier, the "
              "polynomial path");

static_assert(static_cast<int>(crucible::algebra::lattices::FpLibmPolicy::Polynomial) == 6,
              "the polynomial policy must keep ordinal 6");

constexpr std::uint64_t pair_bits(std::pair<float, float> p) noexcept {
    const auto a = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(p.first));
    const auto b = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(p.second));
    return (a << 32) | b;
}

void test_bit_determinism() {
    // Small values, alternating bit patterns, the two halves of the
    // range, and a repeated input: a spread across the input domain
    // rather than a random sample.
    static constexpr std::pair<std::uint32_t, std::uint32_t> pairs[] = {
        {1u, 2u},
        {3u, 5u},
        {7u, 11u},
        {13u, 17u},
        {0x55555555u, 0xAAAAAAAAu},
        {0xCCCCCCCCu, 0x33333333u},
        {0x01234567u, 0x89ABCDEFu},
        {0xFEDCBA98u, 0x76543210u},
        {0xDEADBEEFu, 0xCAFEBABEu},
        {0xBADC0FFEu, 0xE0DDF00Du},
        {0x00000001u, 0xFFFFFFFEu},
        {0x00000002u, 0xFFFFFFFDu},
        {0x80000000u, 0x7FFFFFFFu},
        {0x40000000u, 0xC0000000u},
        {0x12345678u, 0x12345678u},
        {0xABCDEF12u, 0x21FEDCBAu},
    };

    for (auto [u1, u2] : pairs) {
        const auto a = Philox::box_muller_polynomial_det(u1, u2).peek();
        const auto b = Philox::box_muller_polynomial_det(u1, u2).peek();
        if (pair_bits(a) != pair_bits(b)) {
            std::fprintf(stderr,
                         "bit-determinism FAIL: input (0x%08X, 0x%08X) gave "
                         "different bits on repeat: 0x%016lX vs 0x%016lX\n",
                         u1, u2, pair_bits(a), pair_bits(b));
            std::abort();
        }
    }
}

void test_finite_output() {
    // The input mapping shifts u1 away from zero so the logarithm never
    // reaches negative infinity and the square root never takes a negative
    // argument.  A thousand strided cases is enough to reach any path
    // where that mapping fails.
    for (std::uint32_t i = 0; i < 1024; ++i) {
        const std::uint32_t u1 = i * 0xA341316Cu + 0xCBE40D5Bu;
        const std::uint32_t u2 = i * 0xC8013EA4u + 0xD4B49EE5u;
        const auto [z1, z2] = Philox::box_muller_polynomial_det(u1, u2).peek();
        if (!std::isfinite(z1) || !std::isfinite(z2)) {
            std::fprintf(stderr,
                         "finite-output FAIL: input (0x%08X, 0x%08X) → "
                         "(%g, %g) is not finite.\n",
                         u1, u2, static_cast<double>(z1), static_cast<double>(z2));
            std::abort();
        }
    }
}

void test_statistical_sanity() {
    // The tolerances are deliberately loose.  These polynomials are only
    // accurate to about a millionth, so a tight band would fail on their
    // own error.  What a band this wide still catches is a gross fault,
    // such as a sign or a scale wrong in one coefficient.
    constexpr int kPairs = 2048;
    constexpr int kSamples = kPairs * 2;
    constexpr double kMeanTol = 0.05;
    constexpr double kVarTol = 0.10;

    double mean = 0.0;
    double m2 = 0.0;
    int count = 0;
    for (int i = 0; i < kPairs; ++i) {
        // Drawing the inputs from the generator keeps the sample from
        // inheriting the structure of one linear-congruential sequence.
        const Philox::Ctr c = Philox::generate(Philox::Ctr{static_cast<std::uint32_t>(i), 0u, 0u, 0u},
                                               Philox::Key{0x9E3779B9u, 0xBB67AE85u});
        const auto [z1, z2] = Philox::box_muller_polynomial_det(c[0], c[1]).peek();
        for (float z : {z1, z2}) {
            ++count;
            const double delta = static_cast<double>(z) - mean;
            mean += delta / count;
            const double delta2 = static_cast<double>(z) - mean;
            m2 += delta * delta2;
        }
    }
    const double variance = m2 / (kSamples - 1);

    if (std::abs(mean) > kMeanTol) {
        std::fprintf(stderr,
                     "statistical FAIL: sample mean %.6f exceeds tolerance %.3f "
                     "(N=%d).  Polynomial sin/cos/log may have a sign bug.\n",
                     mean, kMeanTol, kSamples);
        std::abort();
    }
    if (std::abs(variance - 1.0) > kVarTol) {
        std::fprintf(stderr,
                     "statistical FAIL: sample variance %.6f deviates from 1.0 "
                     "by more than %.3f (N=%d).  Polynomial log() may have a scale "
                     "bug.\n",
                     variance, kVarTol, kSamples);
        std::abort();
    }
}

void test_header_smoke() { crucible::fixy::fp::runtime_smoke_test(); }

}  // namespace

int main() {
    test_bit_determinism();
    test_finite_output();
    test_statistical_sanity();
    test_header_smoke();
    std::printf("box_muller_polynomial: PASS\n");
    return 0;
}
