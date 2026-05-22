// FIXY-V-095 sentinel TU — Philox::box_muller_polynomial_det runtime
// witness for the cross-platform-bit-stable Box-Muller polar form.
//
// FOUR claims this TU validates:
//
//   1. Compile-time tier wiring.  box_muller_det is at MonotonicClockRead
//      (libm path); box_muller_polynomial_det is at PhiloxRng (crucible-
//      polynomial path).  Asserted up-top so the file won't even compile
//      under a regression that flips them.
//
//   2. Bit-determinism — same uint32 inputs produce IDENTICAL output bits
//      across repeated invocations within the same run.  This is the
//      replay-determinism floor for any compiled kernel that consumes a
//      Box-Muller sample.  Tested over 16 representative input pairs.
//
//   3. Statistical sanity — N=4096 polynomial Box-Muller samples have
//      sample mean within ~0.05 of 0 and sample variance within ~0.1 of
//      1.  This is NOT a tight tolerance (the polynomials are only
//      ~10^-6 accurate vs libm), but it catches gross failures of the
//      polynomial transcendentals (e.g., wrong sign in a coefficient).
//
//   4. Finite-output — every sample is finite (not NaN, not +/-Inf).
//      The (u1, u2) input mapping intentionally avoids u1==0 (which
//      would log(0) → -inf → sqrt(NaN)), but the test verifies the
//      output guarantee anyway across the full uint32 input domain.
//
// This TU does NOT cross-validate against libm box_muller.  The whole
// point of V-095 is that the two paths DO NOT agree at the bit level
// (libm is platform-specific; polynomial is platform-stable).  A
// cross-validate check here would be circular.

#include <crucible/Philox.h>
#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/fixy/fp/Polynomial.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace cwrap = crucible::fixy::wrap;

namespace {

using crucible::Philox;

// ─── 1. Compile-time tier sanity (load-bearing) ──────────────────────

static_assert(
    decltype(Philox::box_muller_det(0u, 0u))::tier
        == cwrap::DetSafeTier_v::MonotonicClockRead,
    "V-095: box_muller_det MUST be MonotonicClockRead-tier (libm path).");

static_assert(
    decltype(Philox::box_muller_polynomial_det(0u, 0u))::tier
        == cwrap::DetSafeTier_v::PhiloxRng,
    "V-095: box_muller_polynomial_det MUST be PhiloxRng-tier (polynomial path).");

// FpLibmPolicy::Polynomial enumerator wired (sanity-check the lattice
// extension from V-095 phase 1 is reachable from this TU).
static_assert(
    static_cast<int>(crucible::algebra::lattices::FpLibmPolicy::Polynomial) == 6,
    "V-095: FpLibmPolicy::Polynomial MUST be the 7th enumerator (ordinal 6).");

// ─── 2. Bit-determinism over representative inputs ────────────────────

// Returns the bit-cast representation of a float pair as two u32s.
constexpr std::uint64_t pair_bits(std::pair<float, float> p) noexcept {
    const auto a = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(p.first));
    const auto b = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(p.second));
    return (a << 32) | b;
}

void test_bit_determinism() {
    // 16 input pairs spanning the uint32 input domain (low, mid, high,
    // alternating bit patterns).  Each pair MUST produce identical
    // output bits across two back-to-back invocations.
    static constexpr std::pair<std::uint32_t, std::uint32_t> pairs[] = {
        {1u, 2u}, {3u, 5u}, {7u, 11u}, {13u, 17u},
        {0x55555555u, 0xAAAAAAAAu}, {0xCCCCCCCCu, 0x33333333u},
        {0x01234567u, 0x89ABCDEFu}, {0xFEDCBA98u, 0x76543210u},
        {0xDEADBEEFu, 0xCAFEBABEu}, {0xBADC0FFEu, 0xE0DDF00Du},
        {0x00000001u, 0xFFFFFFFEu}, {0x00000002u, 0xFFFFFFFDu},
        {0x80000000u, 0x7FFFFFFFu}, {0x40000000u, 0xC0000000u},
        {0x12345678u, 0x12345678u}, {0xABCDEF12u, 0x21FEDCBAu},
    };

    for (auto [u1, u2] : pairs) {
        const auto a = Philox::box_muller_polynomial_det(u1, u2).peek();
        const auto b = Philox::box_muller_polynomial_det(u1, u2).peek();
        if (pair_bits(a) != pair_bits(b)) {
            std::fprintf(stderr,
                "V-095 bit-determinism FAIL: input (0x%08X, 0x%08X) gave "
                "different bits on repeat: 0x%016lX vs 0x%016lX\n",
                u1, u2, pair_bits(a), pair_bits(b));
            std::abort();
        }
    }
}

// ─── 3. Finite-output guarantee ───────────────────────────────────────

void test_finite_output() {
    // Sweep input pairs by Philox-stride; verify every sample is finite.
    // 1024 cases is enough to catch any reachable NaN/Inf path that the
    // (u1+1) * 2^-32 mapping was supposed to prevent.
    for (std::uint32_t i = 0; i < 1024; ++i) {
        const std::uint32_t u1 = i * 0xA341316Cu + 0xCBE40D5Bu;
        const std::uint32_t u2 = i * 0xC8013EA4u + 0xD4B49EE5u;
        const auto [z1, z2] = Philox::box_muller_polynomial_det(u1, u2).peek();
        if (!std::isfinite(z1) || !std::isfinite(z2)) {
            std::fprintf(stderr,
                "V-095 finite-output FAIL: input (0x%08X, 0x%08X) → "
                "(%g, %g) is not finite.\n",
                u1, u2,
                static_cast<double>(z1), static_cast<double>(z2));
            std::abort();
        }
    }
}

// ─── 4. Statistical sanity — sample mean ~ 0, variance ~ 1 ────────────

void test_statistical_sanity() {
    // Generate N=4096 polynomial Box-Muller samples (2048 input pairs,
    // 2 outputs per pair).  Compute Welford-style streaming mean +
    // sample variance.
    constexpr int    kPairs    = 2048;
    constexpr int    kSamples  = kPairs * 2;
    constexpr double kMeanTol = 0.05;
    constexpr double kVarTol  = 0.10;

    double mean = 0.0;
    double m2   = 0.0;
    int    count = 0;
    for (int i = 0; i < kPairs; ++i) {
        // Use a Philox-derived stream of inputs to decorrelate the test
        // from a particular linear-congruential pattern.
        const Philox::Ctr c = Philox::generate(
            Philox::Ctr{static_cast<std::uint32_t>(i), 0u, 0u, 0u},
            Philox::Key{0x9E3779B9u, 0xBB67AE85u});
        const auto [z1, z2] =
            Philox::box_muller_polynomial_det(c[0], c[1]).peek();
        for (float z : {z1, z2}) {
            ++count;
            const double delta = static_cast<double>(z) - mean;
            mean += delta / count;
            const double delta2 = static_cast<double>(z) - mean;
            m2   += delta * delta2;
        }
    }
    const double variance = m2 / (kSamples - 1);

    if (std::abs(mean) > kMeanTol) {
        std::fprintf(stderr,
            "V-095 statistical FAIL: sample mean %.6f exceeds tolerance %.3f "
            "(N=%d).  Polynomial sin/cos/log may have a sign bug.\n",
            mean, kMeanTol, kSamples);
        std::abort();
    }
    if (std::abs(variance - 1.0) > kVarTol) {
        std::fprintf(stderr,
            "V-095 statistical FAIL: sample variance %.6f deviates from 1.0 "
            "by more than %.3f (N=%d).  Polynomial log() may have a scale "
            "bug.\n", variance, kVarTol, kSamples);
        std::abort();
    }
}

// ─── 5. Header runtime smoke (per discipline) ─────────────────────────

void test_header_smoke() {
    // Exercise the header's own runtime smoke test — proves the
    // sin_poly / cos_poly / log_poly building blocks fire without
    // hitting any abort-path.
    crucible::fixy::fp::runtime_smoke_test();
}

}  // namespace

int main() {
    test_bit_determinism();
    test_finite_output();
    test_statistical_sanity();
    test_header_smoke();
    std::printf("V-095 box_muller_polynomial sentinel: PASS\n");
    return 0;
}
