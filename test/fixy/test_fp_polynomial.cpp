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

#include <fixy/fp/Polynomial.h>

#include <bit>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

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
    using namespace fixy::fp::detail::polynomial_self_test;

    volatile std::uint32_t u1 = 0xDEADBEEFu;
    volatile std::uint32_t u2 = 0xCAFEBABEu;
    const auto pair = fp::box_muller_polynomial(u1, u2);

    int failures = 0;
    failures += report("box_muller .first", word(pair.first), kBoxMullerZ1);
    failures += report("box_muller .second", word(pair.second), kBoxMullerZ2);
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
            const auto pair = fp::box_muller_polynomial(a, b);
            if (std::isnan(pair.first) || std::isnan(pair.second)) {
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
// evaluation.  These cases reach them at run time, in a child process,
// and pass only when the child stops by SIGABRT and its standard error
// names a contract violation.

enum class ChildEnd : unsigned char {
    Aborted,
    ExitedZero,
    Other
};

struct ChildResult {
    ChildEnd end = ChildEnd::Other;
    std::string error_text;
};

// Runs the body in a child process, and returns how the child stopped and
// what it wrote to standard error.
[[nodiscard]] ChildResult run_in_child(void (*body)()) {
    ChildResult result;
    int channel[2];
    if (::pipe(channel) != 0) return result;
    const ::pid_t child = ::fork();  // SPAWN-PROCESS-OK: a death test observes the abort in a child
    if (child < 0) return result;
    if (child == 0) {
        ::dup2(channel[1], 2);
        ::close(channel[0]);
        body();
        ::_exit(0);
    }
    ::close(channel[1]);
    char buffer[512];
    for (::ssize_t got = ::read(channel[0], buffer, sizeof buffer); got > 0;
         got = ::read(channel[0], buffer, sizeof buffer)) {
        result.error_text.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(channel[0]);
    int status = 0;
    ::waitpid(child, &status, 0);  // SPAWN-PROCESS-OK: the parent reaps the child of the death test
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
        result.end = ChildEnd::Aborted;
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        result.end = ChildEnd::ExitedZero;
    }
    return result;
}

float volatile sink = 0.0f;

// 1e10 is past one turn, and its nearest multiple of pi/2 does not fit
// int32.  Without the precondition the conversion is undefined behaviour.
void sin_poly_past_int32() { sink = fp::sin_poly(opaque(1.0e10f)); }

void cos_poly_just_past_one_turn() { sink = fp::cos_poly(opaque(0x1.921FB8p+2f)); }

void log_poly_of_zero() { sink = fp::log_poly(opaque(0.0f)); }

void sin_poly_at_one_turn() { sink = fp::sin_poly(opaque(fp::kOneTurn)); }

// Reports one case, and returns 0 when the child stopped as expected.
[[nodiscard]] int expect_child(const char* name, void (*body)(), bool must_abort) {
    const ChildResult result = run_in_child(body);
    const bool names_violation = result.error_text.find("contract violation") != std::string::npos;
    const bool is_expected =
        must_abort ? (result.end == ChildEnd::Aborted && names_violation) : result.end == ChildEnd::ExitedZero;
    if (!is_expected) {
        std::fprintf(stderr, "%s: the child %s.\nchild stderr:\n%s\n", name,
                     result.end == ChildEnd::Aborted      ? "aborted"
                     : result.end == ChildEnd::ExitedZero ? "continued and exited with status 0"
                                                          : "stopped in a different way",
                     result.error_text.c_str());
        return 1;
    }
    return 0;
}

[[nodiscard]] int preconditions_refuse_at_run_time() {
    int failures = 0;
    failures += expect_child("sin_poly(1e10)", sin_poly_past_int32, true);
    failures += expect_child("cos_poly of the first float past one turn", cos_poly_just_past_one_turn, true);
    failures += expect_child("log_poly(0)", log_poly_of_zero, true);
    failures += expect_child("sin_poly(one turn)", sin_poly_at_one_turn, false);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main() {
    if (const int rc = contraction_is_what_the_build_claims(); rc != 0) return rc;
    if (const int rc = identities_hold_at_runtime(); rc != 0) return rc;
    if (const int rc = box_muller_pair_matches_the_pin(); rc != 0) return rc;
    if (const int rc = sweep_matches_the_pins(); rc != 0) return rc;
    if (const int rc = every_angle_in_one_turn_is_accurate(); rc != 0) return rc;
    if (const int rc = every_sampled_positive_normal_is_accurate(); rc != 0) return rc;
    if (const int rc = box_muller_survives_the_extreme_words(); rc != 0) return rc;
    if (const int rc = preconditions_refuse_at_run_time(); rc != 0) return rc;
    return 0;
}
