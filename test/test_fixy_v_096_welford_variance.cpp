// Two properties of the variance accumulator in the bench harness are under
// test here, and neither is visible from the production call site.
//
// The closed-form variance, var = (Σx² − (Σx)²/n) / (n − 1), cancels
// catastrophically once the variance is small next to the square of the mean,
// which is the usual shape of timing samples. The two large sums agree to
// within a few units in the last place and their difference is then rounding
// noise rather than the variance. Welford's online update accumulates the
// squared deviations from the running mean directly and never forms those two
// large sums, so no cancellation arises.
//
// The accumulator type is double rather than long double because long double
// is 80-bit extended precision on x86-64 and plain binary64 on AArch64. The
// same samples would otherwise produce different bits on different members of
// one fleet. binary64 is identical on every conforming target.
//
// This file deliberately reaches only one entry point of the harness,
// Percentiles::compute. Nothing here instantiates the bootstrap or rank-test
// paths, so a regression in those cannot show up as a failure of this file.

#include <bench_harness.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

namespace {

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "CRUCIBLE_FP_STRICT_FLOOR is not defined on this TU — crucible_fp_strict INTERFACE is not transitively linked.  Welford correctness relies on no compiler reassociation; without the floor, -ffp-contract=fast could fuse delta*delta2 → an FMA that perturbs accumulator order across compilers."
#endif

static_assert(CRUCIBLE_FP_STRICT_FLOOR == 1, "CRUCIBLE_FP_STRICT_FLOOR must be exactly 1.");

// Widening any of these back to long double, or narrowing one to float, reds
// here before any of the numeric checks below get a chance to run.

static_assert(std::is_same_v<decltype(bench::Percentiles{}.mean), double>,
              "Percentiles::mean must be double. long double is 80-bit on "
              "one half of the fleet and 64-bit on the other.");

static_assert(std::is_same_v<decltype(bench::Percentiles{}.stddev), double>, "Percentiles::stddev must be double.");

static_assert(std::is_same_v<decltype(bench::Percentiles{}.cv), double>, "Percentiles::cv must be double.");

[[nodiscard]] bool approx(double a, double b, double eps) noexcept { return std::abs(a - b) <= eps; }

// The closed-form variance, in the same binary64 precision Welford uses, kept
// here only so the ill-conditioned test can compare against it. It returns
// variance rather than standard deviation, and it is allowed to return a
// negative number: going negative under cancellation is the behaviour being
// demonstrated.
[[nodiscard]] double naive_variance_2pass(const std::vector<double>& xs) noexcept {
    if (xs.size() < 2) return 0.0;
    double sum = 0.0;
    double sumsq = 0.0;
    for (double x : xs) {
        sum += x;
        sumsq += x * x;
    }
    const double n = static_cast<double>(xs.size());
    return (sumsq - (sum * sum) / n) / (n - 1.0);
}

int g_failures = 0;

#define V096_CHECK(cond)                                                                       \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "[FAIL] %s:%d  V096_CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

void test_welford_well_conditioned() {
    // Mean 3, squared deviations 4 + 1 + 0 + 1 + 4 = 10, and the
    // Bessel-corrected variance is 10 / 4 = 2.5.
    std::vector<double> xs{1.0, 2.0, 3.0, 4.0, 5.0};
    const bench::Percentiles p = bench::Percentiles::compute(xs);

    V096_CHECK(p.n == 5);
    V096_CHECK(approx(p.mean, 3.0, 1e-12));
    V096_CHECK(approx(p.stddev, std::sqrt(2.5), 1e-12));
    V096_CHECK(approx(p.cv, std::sqrt(2.5) / 3.0, 1e-12));

    // At this magnitude cancellation has not bitten yet, so the closed form
    // reaches the same answer. That is what makes the divergence in the next
    // test attributable to conditioning and not to a coding error here.
    std::vector<double> xs_copy{1.0, 2.0, 3.0, 4.0, 5.0};
    const double naive_var = naive_variance_2pass(xs_copy);
    V096_CHECK(approx(naive_var, 2.5, 1e-12));
}

void test_welford_ill_conditioned() {
    // Variance is translation-invariant, so shifting the same five samples by
    // a billion leaves the true variance at 2.5. The sum of squares is now
    // about five times 10^18 and the squared sum over n is the same
    // magnitude, so their difference of 10 falls off the end of binary64.
    constexpr double kBase = 1.0e9;
    std::vector<double> xs{
        kBase + 1.0, kBase + 2.0, kBase + 3.0, kBase + 4.0, kBase + 5.0,
    };

    const bench::Percentiles p = bench::Percentiles::compute(xs);

    V096_CHECK(p.n == 5);
    V096_CHECK(approx(p.mean, kBase + 3.0, 1e-3));
    // The tolerance is 1e-3 because that is roughly one unit in the last place
    // at this magnitude, not because the answer is imprecise.
    V096_CHECK(approx(p.stddev, std::sqrt(2.5), 1e-3));
    // Under the closed form the failure lands as sqrt of a clamped negative,
    // which is zero rather than NaN, so a finiteness check alone would pass.
    // The lower bound is what distinguishes the two.
    V096_CHECK(std::isfinite(p.stddev));
    V096_CHECK(p.stddev > 1.0);

    std::vector<double> xs_copy = xs;
    const double naive_var = naive_variance_2pass(xs_copy);

    // Same inputs, same precision, same compiler flags. Only the algorithm
    // differs, and the closed form must visibly break: either the sign flips
    // or the magnitude is wrong by order one. Either outcome shows the
    // accumulator choice is load-bearing.
    const bool naive_failed = (naive_var <= 0.0) || (std::abs(naive_var - 2.5) > 1.0);
    V096_CHECK(naive_failed);

    // A thousandfold margin, rather than equality against a fixed tolerance,
    // keeps the claim about the gap between the two algorithms and not about
    // either one's absolute accuracy.
    const double welford_err = std::abs(p.stddev - std::sqrt(2.5));
    const double naive_err = std::abs(std::sqrt(std::max(0.0, naive_var)) - std::sqrt(2.5));
    V096_CHECK(welford_err * 1000.0 < naive_err + 1e-12);
}

// The online update is order-dependent, and the order is fixed by the sort
// that compute() performs, so the same multiset of samples yields the same
// bits on every run.
void test_welford_deterministic() {
    std::vector<double> xs_a{17.0, 23.0, 31.0, 41.0, 59.0, 67.0, 73.0};
    std::vector<double> xs_b = xs_a;

    const bench::Percentiles a = bench::Percentiles::compute(xs_a);
    const bench::Percentiles b = bench::Percentiles::compute(xs_b);

    // Comparing the copied-out bits rather than the doubles gives exact
    // equality and keeps the float-equality diagnostic satisfied.
    auto bits = [](double x) -> std::uint64_t {
        std::uint64_t out = 0;
        static_assert(sizeof(out) == sizeof(x));
        __builtin_memcpy(&out, &x, sizeof(x));
        return out;
    };
    V096_CHECK(bits(a.mean) == bits(b.mean));
    V096_CHECK(bits(a.stddev) == bits(b.stddev));
    V096_CHECK(bits(a.cv) == bits(b.cv));
}

}  // namespace

int main() {
    test_welford_well_conditioned();
    test_welford_ill_conditioned();
    test_welford_deterministic();

    if (g_failures == 0) {
        std::printf("Welford variance sentinel: PASS\n");
        return 0;
    }
    std::fprintf(stderr,
                 "Welford variance sentinel: FAIL "
                 "(%d V096_CHECK failure(s))\n",
                 g_failures);
    return 1;
}
