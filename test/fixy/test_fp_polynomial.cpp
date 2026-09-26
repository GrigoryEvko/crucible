// The hand-written transcendentals, exercised with non-constant operands.
//
// fixy/fp/Polynomial.h pins every value this file pins, as a
// static_assert over a constant-folded call.  That is the stronger check
// and it is not the same check: a constant evaluation runs GCC's
// compile-time arithmetic, and a runtime call runs the host's FPU with
// whatever the optimizer did to the expression.  The two agreeing is the
// claim, and it is only a claim if both are made.
//
// This source builds twice.  test_fp_polynomial builds with the
// -ffp-contract=off of the tree.  test_fp_polynomial_contracted builds
// with -ffp-contract=on and fused multiply-add instructions, where a
// written a * b + c can fuse.  The two builds compare one sweep of every
// public function against the same pinned digests, so they agree with
// each other and with constant evaluation.  Each build also checks that
// its contraction setting is the one it claims, so a flag that drops out
// of the build fails the test instead of making it vacuous.
//
// Old spelling: test/test_fixy_v_095_box_muller_polynomial.cpp, which
// called fixy::fp::runtime_smoke_test() and asserted nothing about what
// came back.

#include <fixy/fp/Polynomial.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace fp = fixy::fp;

namespace {

// Reading through a volatile keeps the optimizer from folding the call
// into the constant the header already pinned.
[[nodiscard]] float opaque(float value) noexcept {
    volatile float sink = value;
    return sink;
}

[[nodiscard]] std::uint32_t opaque_word(std::uint32_t value) noexcept {
    volatile std::uint32_t sink = value;
    return sink;
}

[[nodiscard]] std::uint32_t word(float value) noexcept { return std::bit_cast<std::uint32_t>(value); }

[[nodiscard]] int report(const char* what, std::uint32_t got, std::uint32_t want) {
    if (got != want) {
        std::fprintf(stderr, "%s: got 0x%08X, want 0x%08X\n", what, got, want);
        return 1;
    }
    return 0;
}

[[nodiscard]] int report_digest(const char* what, std::uint64_t got, std::uint64_t want) {
    if (got != want) {
        std::fprintf(stderr, "%s: got 0x%016llX, want 0x%016llX\n", what, static_cast<unsigned long long>(got),
                     static_cast<unsigned long long>(want));
        return 1;
    }
    return 0;
}

// ── The contraction setting of this build ───────────────────────────

// The product of 1 + 2^-23 and 1 - 2^-23 is 1 - 2^-46.  With c = -1, a
// fused multiply-add gives -2^-46 and a rounded product gives zero, so
// the result tells whether this build contracts a written a * b + c.
[[nodiscard]] float written_multiply_add(float a, float b, float c) noexcept { return a * b + c; }

[[nodiscard]] int contraction_is_what_the_build_claims() {
    const float result = written_multiply_add(opaque(0x1.000002p0f), opaque(0x1.fffffcp-1f), opaque(-1.0f));
#if defined(CRUCIBLE_TEST_FP_CONTRACTED)
    const float fused = -0x1.0p-46f;
    return report("a * b + c in the contracted build must fuse", word(result), word(fused));
#else
    const float rounded = 0.0f;
    return report("a * b + c in the build without contraction must not fuse", word(result), word(rounded));
#endif
}

// ── The sweep ───────────────────────────────────────────────────────

// A fixed stream of words, the same on every platform: xorshift32.
[[nodiscard]] constexpr std::uint32_t next_word(std::uint32_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// A fold in the style of FNV-1a over the bit pattern of each value, one
// 32-bit word for each step.
[[nodiscard]] constexpr std::uint64_t fold(std::uint64_t digest, float value) noexcept {
    return (digest ^ std::bit_cast<std::uint32_t>(value)) * 0x100000001B3ULL;
}

inline constexpr std::uint32_t kTwoPiBits = std::bit_cast<std::uint32_t>(0x1.921FB6p+2f);
inline constexpr std::uint32_t kSignBit = 0x80000000u;
inline constexpr std::uint32_t kSmallestNormalBits = 0x00800000u;
inline constexpr std::uint32_t kLargestNormalBits = 0x7F7FFFFFu;

// Folds every public function over `samples` inputs from the word stream:
// box_muller_polynomial on two raw words, sin_poly and cos_poly on an
// angle whose bit pattern is uniform up to 2*pi with a random sign, and
// log_poly on a bit pattern uniform over the positive normal floats.  The
// angles include zeros and subnormals.  O(samples).
[[nodiscard]] constexpr std::uint64_t sweep_digest(std::uint32_t seed, std::uint32_t samples) noexcept {
    std::uint64_t digest = 0xCBF29CE484222325ULL;
    std::uint32_t state = seed;
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        const std::uint32_t first_word = next_word(state);
        const std::uint32_t second_word = next_word(state);
        const auto [first_normal, second_normal] = fp::box_muller_polynomial(first_word, second_word);
        digest = fold(digest, first_normal);
        digest = fold(digest, second_normal);

        const std::uint32_t angle_word = next_word(state);
        const float angle = std::bit_cast<float>((angle_word % (kTwoPiBits + 1u)) | (angle_word & kSignBit));
        digest = fold(digest, fp::sin_poly(angle));
        digest = fold(digest, fp::cos_poly(angle));

        const std::uint32_t log_word = next_word(state);
        const float positive_normal =
            std::bit_cast<float>(kSmallestNormalBits + log_word % (kLargestNormalBits - kSmallestNormalBits + 1u));
        digest = fold(digest, fp::log_poly(positive_normal));
    }
    return digest;
}

inline constexpr std::uint32_t kSweepSeed = 0x9E3779B9u;

// The short sweep runs in constant evaluation here and at run time below.
inline constexpr std::uint32_t kShortSweepSamples = 1024;
inline constexpr std::uint64_t kShortSweepDigest = 0x7C5E65A2B8064D35ULL;
static_assert(sweep_digest(kSweepSeed, kShortSweepSamples) == kShortSweepDigest,
              "the constant-evaluated sweep moved. A change to the arithmetic of fixy/fp/Polynomial.h moves "
              "this digest: if that change was on purpose, re-measure and say so in the commit.");

// The long sweep is too long for constant evaluation.  Both builds must
// reach the same digest.
inline constexpr std::uint32_t kLongSweepSamples = 1u << 17;
inline constexpr std::uint64_t kLongSweepDigest = 0xEBA0C1BEB4DB4EC1ULL;

[[nodiscard]] int sweep_matches_the_pins() {
    int failures = 0;
    failures +=
        report_digest("the short sweep at run time, against constant evaluation",
                      sweep_digest(opaque_word(kSweepSeed), opaque_word(kShortSweepSamples)), kShortSweepDigest);
    failures += report_digest("the long sweep at run time",
                              sweep_digest(opaque_word(kSweepSeed), opaque_word(kLongSweepSamples)), kLongSweepDigest);
    return failures == 0 ? 0 : 1;
}

// ── The pinned values ───────────────────────────────────────────────

[[nodiscard]] int identities_hold_at_runtime() {
    int failures = 0;
    failures += report("log_poly(1)", word(fp::log_poly(opaque(1.0f))), word(0.0f));
    failures += report("sin_poly(0)", word(fp::sin_poly(opaque(0.0f))), word(0.0f));
    failures += report("sin_poly(-0)", word(fp::sin_poly(opaque(-0.0f))), word(-0.0f));
    failures += report("cos_poly(0)", word(fp::cos_poly(opaque(0.0f))), word(1.0f));
    failures += report("log_poly(2)", word(fp::log_poly(opaque(2.0f))), word(0x1.62e43p-1f));
    failures += report("sin_poly(pi/2)", word(fp::sin_poly(opaque(0x1.921FB6p+0f))), word(1.0f));
    failures += report("cos_poly(pi)", word(fp::cos_poly(opaque(0x1.921FB6p+1f))), word(-1.0f));
    return failures == 0 ? 0 : 1;
}

[[nodiscard]] int box_muller_pair_matches_the_pin() {
    // The two words come from the header, so this cell fails if the pin
    // moves, and fails differently if the runtime path disagrees with the
    // constant-folded one.
    using namespace fixy::fp::detail::polynomial_self_test;

    volatile std::uint32_t u1 = 0xDEADBEEFu;
    volatile std::uint32_t u2 = 0xCAFEBABEu;
    const auto pair = fp::box_muller_polynomial(u1, u2);

    int failures = 0;
    failures += report("box_muller .first", word(pair.first), kBoxMullerZ1);
    failures += report("box_muller .second", word(pair.second), kBoxMullerZ2);
    return failures == 0 ? 0 : 1;
}

// The quadrant is masked to two bits, so no argument can reach the
// std::unreachable() arm.  A runtime sweep is what says so for arguments
// a constant expression would not reach.
[[nodiscard]] int every_quadrant_is_in_range() {
    for (int step = -2000; step <= 2000; ++step) {
        const float x = opaque(static_cast<float>(step) * 0.37f);
        const auto rr = fp::reduce_quarter_pi(x);
        if (rr.quadrant < 0 || rr.quadrant > 3) {
            std::fprintf(stderr, "reduce_quarter_pi(%d * 0.37) reported quadrant %d, outside [0,3]\n", step,
                         rr.quadrant);
            return 1;
        }
        // Both consumers must return a finite number for every quadrant.
        const float s = fp::sin_poly(x);
        const float c = fp::cos_poly(x);
        if (std::isnan(s) || std::isnan(c)) {
            std::fprintf(stderr, "sin_poly/cos_poly returned NaN at %d * 0.37\n", step);
            return 1;
        }
    }
    return 0;
}

// The header states the accuracy of log_poly, measured over every
// positive normal float: 8.4e-5.  This sweep checks the same bound
// against double-precision libm at run time.
inline constexpr double kLogTolerance = 8.4e-5;

[[nodiscard]] int every_sampled_positive_normal_is_accurate() {
    std::uint32_t state = opaque_word(kSweepSeed);
    for (int sample = 0; sample < 1 << 16; ++sample) {
        const std::uint32_t bits =
            kSmallestNormalBits + next_word(state) % (kLargestNormalBits - kSmallestNormalBits + 1u);
        const float value = std::bit_cast<float>(bits);
        const double error = std::fabs(static_cast<double>(fp::log_poly(value)) - std::log(double{value}));
        if (!(error <= kLogTolerance)) {
            std::fprintf(stderr, "log_poly(%a) is off by %.3e, more than %.1e\n", static_cast<double>(value), error,
                         kLogTolerance);
            return 1;
        }
    }
    return 0;
}

// box_muller feeds the logarithm a value derived from a raw word, and the
// added one is what keeps that argument above zero.  A raw zero is the
// case that would have produced log(0).
[[nodiscard]] int box_muller_survives_the_extreme_words() {
    const std::uint32_t words[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
    for (const std::uint32_t w1 : words) {
        for (const std::uint32_t w2 : words) {
            volatile std::uint32_t a = w1;
            volatile std::uint32_t b = w2;
            const auto pair = fp::box_muller_polynomial(a, b);
            if (std::isnan(pair.first) || std::isnan(pair.second)) {
                std::fprintf(stderr, "box_muller(0x%08X, 0x%08X) produced NaN\n", w1, w2);
                return 1;
            }
        }
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = contraction_is_what_the_build_claims(); rc != 0) return rc;
    if (const int rc = identities_hold_at_runtime(); rc != 0) return rc;
    if (const int rc = box_muller_pair_matches_the_pin(); rc != 0) return rc;
    if (const int rc = sweep_matches_the_pins(); rc != 0) return rc;
    if (const int rc = every_quadrant_is_in_range(); rc != 0) return rc;
    if (const int rc = every_sampled_positive_normal_is_accurate(); rc != 0) return rc;
    if (const int rc = box_muller_survives_the_extreme_words(); rc != 0) return rc;
    return 0;
}
