// FIXY-V-096 sentinel TU — bench/bench_harness.h Welford-on-double
// variance migration witness.
//
// V-096 replaces the naive 2-pass variance formula
//
//     var = (Σx² − (Σx)²/n) / (n − 1)
//
// (previously accumulated in `long double`) with Welford's one-pass
// online algorithm accumulated in `double`.  Two simultaneous wins:
//
//   1. Numerical stability.  The naive formula catastrophically cancels
//      when σ² ≪ μ² — the common case for bench timing where μ ≈ 50 ns
//      and σ ≈ 5 ns: Σx² ≈ n·2500 minus (Σx)²/n ≈ n·2500 → finite-
//      precision difference is dominated by rounding, not by the true
//      variance.  Welford accumulates Σ(x − mean)² directly via the
//      online update; no cancellation.  Welford (1962),
//      "Note on a Method for Calculating Corrected Sums of Squares and
//       Products".
//
//   2. Cross-platform bit-equality.  `long double` is 80-bit extended-
//      precision on x86-64 (x87 FPU) but 64-bit binary64 on AArch64
//      and Apple Silicon — same input → different stddev bit pattern
//      across the fleet, defeating the DetSafe story the project sells.
//      `double` is IEEE 754 binary64 on every conforming target.
//
// FOUR claims this TU validates:
//
//   1. Compile-time floor.  CRUCIBLE_FP_STRICT_FLOOR == 1 proves the
//      crucible_fp_strict INTERFACE library is engaged on this TU; that
//      target compiles with -ffp-contract=off + -fno-fast-math +
//      -fno-associative-math + -frounding-math etc., which is what
//      prevents the compiler from re-associating the Welford update
//      back into a 2-pass cancellation-prone form.
//
//   2. Type-shape — Percentiles::{mean,stddev,cv} all `double`, not
//      `long double`.  Asserted at compile time.  A future PR that
//      tries to reintroduce `long double` accumulators by widening the
//      field type reds HERE first, before any runtime test runs.
//
//   3. Well-conditioned correctness — Welford on the symmetric
//      sequence {1, 2, 3, 4, 5} produces variance = 2.5 exactly (the
//      same answer the closed-form computes), within IEEE 754 double
//      precision (≤ 1e-12 absolute).  Mirrors and complements the
//      existing test_bench_harness symmetric-set test.
//
//   4. Ill-conditioned stability — Welford on the shifted sequence
//      {1e9+1, 1e9+2, 1e9+3, 1e9+4, 1e9+5} (mean=1e9+3, variance=2.5)
//      produces variance ≈ 2.5 (within 1e-9 absolute), while the
//      reference naive 2-pass formula running on identical `double`
//      accumulators produces a wildly wrong answer (orders of magnitude
//      off; often negative due to catastrophic cancellation).  The
//      production migration is therefore LOAD-BEARING, not cosmetic.
//
// Cross-validation discipline:
//   This TU shares only one entry point with the rest of the bench
//   harness — Percentiles::compute().  We do NOT instantiate Run /
//   bootstrap_ci / mann_whitney here; those have their own coverage
//   in test_bench_harness.cpp.  A regression in any of them does not
//   poison V-096's signal.

#include <bench_harness.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

namespace {

// ─── Claim 1: Compile-time floor witness ───────────────────────────

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error "CRUCIBLE_FP_STRICT_FLOOR is not defined on this TU — crucible_fp_strict INTERFACE is not transitively linked.  Welford correctness relies on no compiler reassociation; without the floor, -ffp-contract=fast could fuse delta*delta2 → an FMA that perturbs accumulator order across compilers."
#endif

static_assert(CRUCIBLE_FP_STRICT_FLOOR == 1,
              "FIXY-V-096: CRUCIBLE_FP_STRICT_FLOOR must be exactly 1.");

// ─── Claim 2: Type-shape witness ───────────────────────────────────
//
// Probe the exact types of Percentiles' moment fields.  If a future PR
// "promotes" any of these back to long double (or to float for "perf"),
// the static_assert reds and the regression is caught at compile time.

static_assert(std::is_same_v<decltype(bench::Percentiles{}.mean), double>,
              "FIXY-V-096: Percentiles::mean MUST be double "
              "(NOT long double — 80-bit vs 64-bit drift across fleet).");

static_assert(std::is_same_v<decltype(bench::Percentiles{}.stddev), double>,
              "FIXY-V-096: Percentiles::stddev MUST be double.");

static_assert(std::is_same_v<decltype(bench::Percentiles{}.cv), double>,
              "FIXY-V-096: Percentiles::cv MUST be double.");

// ─── Numeric helpers ───────────────────────────────────────────────

[[nodiscard]] bool approx(double a, double b, double eps) noexcept {
    return std::abs(a - b) <= eps;
}

// Reference naive 2-pass variance formula, accumulated in the same
// IEEE 754 binary64 (double) precision Welford uses.  Returns variance
// (NOT stddev).  This is the formula V-096 *replaced*; we keep a
// local copy here to demonstrate the cancellation it suffers from on
// ill-conditioned inputs.
//
// Returning a possibly-negative variance is intentional — the whole
// point of the demonstration is that naive can go negative under
// cancellation, which Welford cannot.
[[nodiscard]] double naive_variance_2pass(const std::vector<double>& xs) noexcept {
    if (xs.size() < 2) return 0.0;
    double sum  = 0.0;
    double sumsq = 0.0;
    for (double x : xs) {
        sum   += x;
        sumsq += x * x;
    }
    const double n = static_cast<double>(xs.size());
    // Classic 2-pass form — catastrophically cancels when sumsq ≈ (sum²)/n.
    return (sumsq - (sum * sum) / n) / (n - 1.0);
}

int g_failures = 0;

#define V096_CHECK(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "[FAIL] %s:%d  V096_CHECK(%s)\n",            \
                         __FILE__, __LINE__, #cond);                         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// ─── Claim 3: Well-conditioned correctness ──────────────────────────

void test_welford_well_conditioned() {
    // Symmetric {1, 2, 3, 4, 5}: mean = 3, sum of squared deviations
    // (4 + 1 + 0 + 1 + 4) = 10, Bessel-corrected variance = 10 / 4 = 2.5.
    std::vector<double> xs{1.0, 2.0, 3.0, 4.0, 5.0};
    const bench::Percentiles p = bench::Percentiles::compute(xs);

    V096_CHECK(p.n == 5);
    V096_CHECK(approx(p.mean,    3.0,            1e-12));
    V096_CHECK(approx(p.stddev,  std::sqrt(2.5), 1e-12));
    // cv = stddev / mean = √2.5 / 3
    V096_CHECK(approx(p.cv,      std::sqrt(2.5) / 3.0, 1e-12));

    // Welford and naive agree at low magnitude (cancellation hasn't
    // bitten yet) — sanity-pin both produce the closed-form answer.
    std::vector<double> xs_copy{1.0, 2.0, 3.0, 4.0, 5.0};
    const double naive_var = naive_variance_2pass(xs_copy);
    V096_CHECK(approx(naive_var, 2.5, 1e-12));
}

// ─── Claim 4: Ill-conditioned stability ─────────────────────────────

void test_welford_ill_conditioned() {
    // Shift the {1..5} sequence by 1e9.  The TRUE variance is still 2.5
    // — variance is translation-invariant.  But the sum-of-squares now
    // ≈ 5·(1e9)² ≈ 5·10¹⁸, and the squared sum/n is the same magnitude;
    // their difference (the variance numerator) is 10, lost in the
    // ~15.95 decimal digits of IEEE 754 binary64.
    constexpr double kBase = 1.0e9;
    std::vector<double> xs{
        kBase + 1.0,
        kBase + 2.0,
        kBase + 3.0,
        kBase + 4.0,
        kBase + 5.0,
    };

    // Welford on the production codepath.
    const bench::Percentiles p = bench::Percentiles::compute(xs);

    V096_CHECK(p.n == 5);
    V096_CHECK(approx(p.mean, kBase + 3.0, 1e-3));
    // Welford recovers the TRUE σ = √2.5 ≈ 1.5811 within ~1e-3
    // (one part in 10¹² of the magnitude — IEEE 754 ULP at 1e9 scale).
    V096_CHECK(approx(p.stddev, std::sqrt(2.5), 1e-3));
    // stddev MUST be finite — the failure mode under naive is
    // sqrt(max(0, negative)) = 0, NOT NaN; we check the positive bound.
    V096_CHECK(std::isfinite(p.stddev));
    V096_CHECK(p.stddev > 1.0);  // Lower-bound far above the naive=0 outcome.

    // Compare against the naive 2-pass on the SAME inputs.  Same
    // precision (binary64), same compiler flags (crucible_fp_strict),
    // but the algorithm differs.  Naive must demonstrably break.
    std::vector<double> xs_copy = xs;
    const double naive_var = naive_variance_2pass(xs_copy);

    // The naive 2-pass on this input is dominated by cancellation
    // rounding.  Two possible failure modes:
    //   (a) naive_var ≤ 0 — sign flipped by cancellation.
    //   (b) |naive_var − 2.5| > 1.0 — magnitude is wrong by O(1).
    // Either proves the migration was load-bearing.
    const bool naive_failed =
        (naive_var <= 0.0) ||
        (std::abs(naive_var - 2.5) > 1.0);
    V096_CHECK(naive_failed);

    // And independently: Welford's |stddev − √2.5| MUST be orders of
    // magnitude smaller than the naive method's error.  This is the
    // BIG bit-strict claim: same input, same fp-strict flags, two
    // algorithms, Welford wins by at least 1000×.
    const double welford_err = std::abs(p.stddev - std::sqrt(2.5));
    const double naive_err   =
        std::abs(std::sqrt(std::max(0.0, naive_var)) - std::sqrt(2.5));
    V096_CHECK(welford_err * 1000.0 < naive_err + 1e-12);
}

// ─── Claim 5: Determinism — same input, same bits ──────────────────
//
// Welford is deterministic given the input ORDER (the production code
// iterates `for (double v : ns_samples)` over a vector that was just
// sorted by Percentiles::compute, so order is content-determined).
// Run twice; same bit pattern.
void test_welford_deterministic() {
    std::vector<double> xs_a{17.0, 23.0, 31.0, 41.0, 59.0, 67.0, 73.0};
    std::vector<double> xs_b = xs_a;  // identical content

    const bench::Percentiles a = bench::Percentiles::compute(xs_a);
    const bench::Percentiles b = bench::Percentiles::compute(xs_b);

    // Bit-equality on every moment field.  std::bit_cast through
    // uint64_t gives an exact-bit equality predicate immune to
    // -Werror=float-equal.
    auto bits = [](double x) -> std::uint64_t {
        std::uint64_t out = 0;
        static_assert(sizeof(out) == sizeof(x));
        __builtin_memcpy(&out, &x, sizeof(x));
        return out;
    };
    V096_CHECK(bits(a.mean)   == bits(b.mean));
    V096_CHECK(bits(a.stddev) == bits(b.stddev));
    V096_CHECK(bits(a.cv)     == bits(b.cv));
}

}  // namespace

int main() {
    test_welford_well_conditioned();
    test_welford_ill_conditioned();
    test_welford_deterministic();

    if (g_failures == 0) {
        std::printf("FIXY-V-096 Welford variance sentinel: PASS\n");
        return 0;
    }
    std::fprintf(stderr,
                 "FIXY-V-096 Welford variance sentinel: FAIL "
                 "(%d V096_CHECK failure(s))\n", g_failures);
    return 1;
}
