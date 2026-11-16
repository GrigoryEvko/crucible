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

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/fp/Polynomial.h>
#include <foundation/effects/Effect.h>

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

inline constexpr std::uint32_t kOneTurnBits = std::bit_cast<std::uint32_t>(fp::kOneTurn);
inline constexpr std::uint32_t kSignBit = 0x80000000u;
inline constexpr std::uint32_t kSmallestNormalBits = 0x00800000u;
inline constexpr std::uint32_t kLargestNormalBits = 0x7F7FFFFFu;

// Folds every public function over `samples` inputs from the word stream:
// box_muller_polynomial on two raw words, sin_poly and cos_poly on an
// angle whose bit pattern is uniform over one turn with a random sign, and
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
        const float angle = std::bit_cast<float>((angle_word % (kOneTurnBits + 1u)) | (angle_word & kSignBit));
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
    using namespace fixy::fp::detail;

    volatile std::uint32_t u1 = 0xDEADBEEFu;
    volatile std::uint32_t u2 = 0xCAFEBABEu;
    const auto [first_normal, second_normal] = fp::box_muller_polynomial(u1, u2);

    int failures = 0;
    failures += report("box_muller first value", word(first_normal), kBoxMullerZ1);
    failures += report("box_muller second value", word(second_normal), kBoxMullerZ2);
    return failures == 0 ? 0 : 1;
}

// ── The domains and the accuracy on them ────────────────────────────

// The header states the accuracy on each domain, measured over every
// float in it: 2.0e-7 for sin_poly and cos_poly within one turn, and
// 8.4e-5 for log_poly over the positive normals.  This sweep checks the
// same bounds against double-precision libm at run time.  It also checks
// that every quadrant is in range and no result is a NaN.
inline constexpr double kTrigTolerance = 2.0e-7;
inline constexpr double kLogTolerance = 8.4e-5;

[[nodiscard]] int every_angle_in_one_turn_is_accurate() {
    constexpr int steps = 4096;
    for (int step = -steps; step <= steps; ++step) {
        // step / steps is at most 1 in magnitude, so the product is at
        // most one turn.
        const float angle = opaque(fp::kOneTurn * (static_cast<float>(step) / static_cast<float>(steps)));
        const auto reduction = fp::reduce_quarter_pi(angle);
        if (reduction.quadrant < 0 || reduction.quadrant > 3) {
            std::fprintf(stderr, "reduce_quarter_pi(%a) reported quadrant %d, outside [0,3]\n",
                         static_cast<double>(angle), reduction.quadrant);
            return 1;
        }
        const double sine_error = std::fabs(static_cast<double>(fp::sin_poly(angle)) - std::sin(double{angle}));
        const double cosine_error = std::fabs(static_cast<double>(fp::cos_poly(angle)) - std::cos(double{angle}));
        if (!(sine_error <= kTrigTolerance) || !(cosine_error <= kTrigTolerance)) {
            std::fprintf(stderr, "at %a: sin_poly is off by %.3e and cos_poly by %.3e, more than %.1e\n",
                         static_cast<double>(angle), sine_error, cosine_error, kTrigTolerance);
            return 1;
        }
    }
    return 0;
}

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
            const auto [first_normal, second_normal] = fp::box_muller_polynomial(a, b);
            if (std::isnan(first_normal) || std::isnan(second_normal)) {
                std::fprintf(stderr, "box_muller(0x%08X, 0x%08X) produced NaN\n", w1, w2);
                return 1;
            }
        }
    }
    return 0;
}

// ── The preconditions at run time ───────────────────────────────────
//
// test/fixy/neg/neg_fp_* reach each precondition in a constant
// evaluation.  These cases reach them at run time, in a child process
// through expect_dies of the Check family.  Each passes only when the child
// stops by SIGABRT and its standard error names the violated precondition.
// The control runs in the test process, and it must not abort.

float volatile sink = 0.0f;

// 1e10 is past one turn, and its nearest multiple of pi/2 does not fit
// int32.  Without the precondition the conversion is undefined behaviour.
void sin_poly_past_int32() { sink = fp::sin_poly(opaque(1.0e10f)); }

void cos_poly_just_past_one_turn() { sink = fp::cos_poly(opaque(0x1.921FB8p+2f)); }

void log_poly_of_zero() { sink = fp::log_poly(opaque(0.0f)); }

void sin_poly_at_one_turn() { sink = fp::sin_poly(opaque(fp::kOneTurn)); }

void preconditions_refuse_at_run_time(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kOutsideOneTurn{"foundation: contract violation: is_within_one_turn(angle)"};
    ::fixy::check::expect_dies(test, sin_poly_past_int32, kOutsideOneTurn);
    ::fixy::check::expect_dies(test, cos_poly_just_past_one_turn, kOutsideOneTurn);
    ::fixy::check::expect_dies(test, log_poly_of_zero,
                               ::fixy::TextView{"foundation: contract violation: is_positive_normal(value)"});
    sin_poly_at_one_turn();
    ::fixy::check::pass(test, "  each precondition refuses at run time, and one turn passes\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    if (const int rc = contraction_is_what_the_build_claims(); rc != 0) return rc;
    if (const int rc = identities_hold_at_runtime(); rc != 0) return rc;
    if (const int rc = box_muller_pair_matches_the_pin(); rc != 0) return rc;
    if (const int rc = sweep_matches_the_pins(); rc != 0) return rc;
    if (const int rc = every_angle_in_one_turn_is_accurate(); rc != 0) return rc;
    if (const int rc = every_sampled_positive_normal_is_accurate(); rc != 0) return rc;
    if (const int rc = box_muller_survives_the_extreme_words(); rc != 0) return rc;
    preconditions_refuse_at_run_time(test);
    return 0;
}
