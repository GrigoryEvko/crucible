// The statistical primitives the benchmark harness reports through, and
// the saturating counter delta the performance snapshot exposes.
//
// Each primitive follows a published definition, and the expected values
// below come from those definitions rather than from the current
// implementation:
//
//   percentile interpolation follows R type 7 (Hyndman and Fan, 1996)
//   the confidence interval follows Efron's bootstrap (1979)
//   the comparison follows the Mann-Whitney U test (1947)
//
// The performance header is pulled in for the snapshot type alone.  Its
// hub class is never instantiated here, because the object file that
// defines it is not linked into this test.  The BPF-gated paths in the
// harness are likewise inactive, since the substrate they need is not
// linked either.

#include <algorithm>
#include <bit>
#include "test_assert.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <type_traits>
#include <vector>

#include "bench_harness.h"
#include <crucible/perf/SenseHub.h>

namespace {

// Both the confidence interval and the comparison refuse to run on
// fewer than thirty samples.  kSampleN sits well above that line, so the
// resampling distribution is representative; kSmallN sits below it, to
// reach the refusal itself.
//
// The resample count, the alpha and the seed all match the harness
// defaults, so these tests exercise the same path the benchmarks do
// rather than a configuration nothing else uses.
constexpr std::size_t kSampleN = 100;
constexpr std::size_t kSmallN = 10;
constexpr std::size_t kBootstrapB = 1000;
constexpr double kAlpha = 0.05;
constexpr std::uint64_t kDefaultSeed = 0xBEEFCAFEDEADF00Dull;

int g_failures = 0;

// A failure records itself and lets the run continue, so one invocation
// reports every broken invariant rather than only the first.
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "[FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

// The tolerance is absolute rather than relative, which is sound only
// because every value compared in this file is of order one to a
// hundred.
[[nodiscard]] bool approx(double a, double b, double eps = 1e-9) noexcept { return std::abs(a - b) <= eps; }

// For the places where a seeded generator is expected to reproduce a
// value exactly.  Comparing the bit patterns says what is meant and
// also avoids the project-wide ban on double equality.
[[nodiscard]] bool bit_same(double a, double b) noexcept {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

// Several tests compare results against each other, so they have to be
// reading the same input.  Building it once removes the chance of two
// call sites drifting apart.
[[nodiscard]] const std::vector<double>& ramp_1_to_N() {
    static const std::vector<double> v = [] {
        std::vector<double> out(kSampleN);
        for (std::size_t i = 0; i < kSampleN; ++i) {
            out[i] = static_cast<double>(i + 1);
        }
        return out;
    }();
    return v;
}

// The interval may be returned either wrapped in an optional or as a
// bare struct whose zeroed members mean "none".  These probes accept
// both, because only the branch matching the deduced type is
// instantiated and the other never has to compile.
template <typename CI_t>
void check_ci_is_empty(const CI_t& ci) {
    if constexpr (requires { ci.value(); }) {
        CHECK(!ci.has_value());
    } else {
        CHECK(bit_same(ci.lo, 0.0));
        CHECK(bit_same(ci.hi, 0.0));
    }
}

template <typename CI_t>
void check_ci_stable(const CI_t& a, const CI_t& b) {
    if constexpr (requires { a.value(); }) {
        CHECK(a.has_value());
        CHECK(b.has_value());
        if (a.has_value() && b.has_value()) {
            CHECK(bit_same(a->lo, b->lo));
            CHECK(bit_same(a->hi, b->hi));
            CHECK(a->lo > 0.0);
            CHECK(a->hi >= a->lo);
        }
    } else {
        CHECK(bit_same(a.lo, b.lo));
        CHECK(bit_same(a.hi, b.hi));
        // The input ramps from one upwards, so any interval that
        // brackets its median is strictly positive.
        CHECK(a.lo > 0.0);
        CHECK(a.hi >= a.lo);
    }
}

// The comparison reads only the samples and the percentiles, so a report
// can be assembled directly instead of running a measurement.
[[nodiscard]] bench::Report make_report_from(const char* name, std::vector<double> samples) {
    bench::Report r;
    r.name = name;
    r.samples = samples;
    r.pct = bench::Percentiles::compute(samples);
    return r;
}

void test_percentile_interp() {
    // An empty input returns zero only where the internal assert is
    // compiled out.  In a debug build the assert fires first, so this
    // case is unreachable there.
#ifdef NDEBUG
    {
        const std::vector<double> empty;
        CHECK(approx(bench::percentile_interp(empty, 0.5), 0.0));
    }
#endif

    {
        const std::vector<double> one{42.5};
        CHECK(approx(bench::percentile_interp(one, 0.0), 42.5));
        CHECK(approx(bench::percentile_interp(one, 0.5), 42.5));
        CHECK(approx(bench::percentile_interp(one, 1.0), 42.5));
    }

    // With five samples every quartile lands on a sample, so these
    // expectations involve no interpolation at all.
    {
        const std::vector<double> x{1.0, 2.0, 3.0, 4.0, 5.0};
        CHECK(approx(bench::percentile_interp(x, 0.00), 1.0));
        CHECK(approx(bench::percentile_interp(x, 0.25), 2.0));
        CHECK(approx(bench::percentile_interp(x, 0.50), 3.0));
        CHECK(approx(bench::percentile_interp(x, 0.75), 4.0));
        CHECK(approx(bench::percentile_interp(x, 1.00), 5.0));
    }

    // With four samples the median falls between two of them, which is
    // where the interpolation rule actually shows.
    {
        const std::vector<double> x{1.0, 2.0, 3.0, 4.0};
        CHECK(approx(bench::percentile_interp(x, 0.5), 2.5));
    }
}

void test_percentiles_compute() {
    {
        std::vector<double> empty;
        const bench::Percentiles p = bench::Percentiles::compute(empty);
        CHECK(p.n == 0);
        CHECK(approx(p.p50, 0.0));
        CHECK(approx(p.p99, 0.0));
        CHECK(approx(p.min, 0.0));
        CHECK(approx(p.max, 0.0));
        CHECK(approx(p.mean, 0.0));
        CHECK(approx(p.stddev, 0.0));
        CHECK(approx(p.cv, 0.0));
    }

    // A sample with no spread: every percentile is the same value and
    // the deviation is zero, which is the case a divide-by-spread bug
    // would show up in.
    {
        std::vector<double> uniform{5.0, 5.0, 5.0, 5.0, 5.0};
        const bench::Percentiles p = bench::Percentiles::compute(uniform);
        CHECK(p.n == 5);
        CHECK(approx(p.p50, 5.0));
        CHECK(approx(p.p75, 5.0));
        CHECK(approx(p.p90, 5.0));
        CHECK(approx(p.p95, 5.0));
        CHECK(approx(p.p99, 5.0));
        CHECK(approx(p.p99_9, 5.0));
        CHECK(approx(p.p99_99, 5.0));
        CHECK(approx(p.min, 5.0));
        CHECK(approx(p.max, 5.0));
        CHECK(approx(p.mean, 5.0));
        CHECK(approx(p.stddev, 0.0, 1e-12));
        CHECK(approx(p.cv, 0.0, 1e-12));
    }

    // The expected deviation is the square root of 2.5, which is the
    // variance with Bessel's correction: (4+1+0+1+4)/4.  The input is
    // given out of order so that a missing internal sort shows up in the
    // minimum and maximum.
    {
        std::vector<double> sym{3.0, 1.0, 4.0, 5.0, 2.0};
        const bench::Percentiles p = bench::Percentiles::compute(sym);
        CHECK(p.n == 5);
        CHECK(approx(p.min, 1.0));
        CHECK(approx(p.max, 5.0));
        CHECK(approx(p.mean, 3.0));
        CHECK(approx(p.p50, 3.0));
        CHECK(approx(p.stddev, std::sqrt(2.5), 1e-9));
        CHECK(approx(p.cv, std::sqrt(2.5) / 3.0, 1e-9));
    }
}

// The bounds themselves depend on how the generator draws its
// resamples, so nothing here asserts a numeric interval.  What is
// asserted is that the same seed reproduces the same bounds exactly.
void test_bootstrap_ci() {
    {
        std::vector<double> small(kSmallN);
        for (std::size_t i = 0; i < kSmallN; ++i) {
            small[i] = static_cast<double>(i + 1);
        }
        auto result = bench::bootstrap_ci(small, 0.5, /*B=*/100);
        check_ci_is_empty(result);
    }

    {
        const auto& big = ramp_1_to_N();
        auto a = bench::bootstrap_ci(big, 0.5, kBootstrapB, kAlpha, kDefaultSeed);
        auto b = bench::bootstrap_ci(big, 0.5, kBootstrapB, kAlpha, kDefaultSeed);
        check_ci_stable(a, b);
    }
}

void test_compare() {
    {
        const auto& v = ramp_1_to_N();
        const bench::Report a = make_report_from("a", v);
        const bench::Report b = make_report_from("b", v);
        const bench::Compare c = bench::compare(a, b);

        CHECK(!c.distinguishable);
        CHECK(approx(c.delta_p50_pct, 0.0, 1e-9));
        CHECK(approx(c.delta_p99_pct, 0.0, 1e-9));
        CHECK(approx(c.delta_mean_pct, 0.0, 1e-9));
    }

    // A shift this small may or may not cross the significance
    // threshold, depending on whether the variance formula applies a tie
    // adjustment and a continuity correction.  Asserting
    // distinguishability either way would pin an implementation detail,
    // so only the sign of the shift and the finiteness of the statistic
    // are checked.
    {
        std::vector<double> va(kSampleN);
        std::vector<double> vb(kSampleN);
        for (std::size_t i = 0; i < kSampleN; ++i) {
            const double base = static_cast<double>(i + 1);
            va[i] = base;
            vb[i] = base + 0.01;
        }
        const bench::Report a = make_report_from("a", va);
        const bench::Report b = make_report_from("b+shift", vb);
        const bench::Compare c = bench::compare(a, b);

        CHECK(c.delta_p50_pct > 0.0);
        CHECK(std::isfinite(c.z));
        CHECK(std::isfinite(c.u));
    }

    // The shift here is enormous, and the sample count is still below
    // the threshold.  Size of effect must not buy its way past the
    // refusal.
    {
        constexpr std::size_t kSmallCmpN = 20;
        std::vector<double> small_a(kSmallCmpN);
        std::vector<double> small_b(kSmallCmpN);
        for (std::size_t i = 0; i < kSmallCmpN; ++i) {
            const double base = static_cast<double>(i);
            small_a[i] = base;
            small_b[i] = base + 100.0;
        }
        const bench::Report a = make_report_from("small_a", small_a);
        const bench::Report b = make_report_from("small_b", small_b);
        const bench::Compare c = bench::compare(a, b);

        CHECK(!c.distinguishable);
    }

    // Both arms hold the same two values in equal numbers, which is the
    // worst case for ranking.  Rank-sum symmetry makes the two U values
    // equal to their common mean, so the statistic's numerator is zero
    // and the result is zero no matter which variance formula divides
    // it.
    {
        std::vector<double> tie_a(kSampleN);
        std::vector<double> tie_b(kSampleN);
        for (std::size_t i = 0; i < kSampleN / 2; ++i) {
            tie_a[2 * i] = 1.0;
            tie_a[2 * i + 1] = 2.0;
            tie_b[2 * i] = 1.0;
            tie_b[2 * i + 1] = 2.0;
        }
        const bench::Report a = make_report_from("tie_a", tie_a);
        const bench::Report b = make_report_from("tie_b", tie_b);
        const bench::Compare c = bench::compare(a, b);

        CHECK(!c.distinguishable);
        CHECK(std::abs(c.z) < 2.576);
        CHECK(approx(c.z, 0.0, 1e-9));
    }
}

// A counter that went backwards is a gauge, not a broken counter, so
// subtraction saturates at zero.  The two failure modes it guards
// against are wrapping to near the unsigned maximum, and clamping the
// whole snapshot instead of the one slot that underflowed.
void test_snapshot_subtract() {
    using ::crucible::perf::Snapshot;
    using ::crucible::perf::NUM_COUNTERS;

    {
        Snapshot pre{};
        Snapshot post{};
        for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
            pre.counters[i] = static_cast<std::uint64_t>(i) * 10u;
            post.counters[i] = pre.counters[i] + static_cast<std::uint64_t>(i) + 1u;
        }
        const Snapshot d = post - pre;
        for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
            CHECK(d.counters[i] == static_cast<std::uint64_t>(i) + 1u);
        }
    }

    // Exactly one slot underflows, so the neighbours are what proves the
    // clamp is per-slot.
    {
        Snapshot pre{};
        Snapshot post{};
        for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
            pre.counters[i] = 100u;
            post.counters[i] = 150u;
        }
        // A slot in the middle rather than at either end, so that an
        // off-by-one in the clamp cannot be mistaken for correct.
        constexpr std::size_t k = 17;
        pre.counters[k] = 200u;
        post.counters[k] = 50u;

        const Snapshot d = post - pre;
        for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
            if (i == k) {
                CHECK(d.counters[i] == 0u);
            } else {
                CHECK(d.counters[i] == 50u);
            }
        }
    }

    // The largest underflow representable, at every slot at once.
    {
        Snapshot pre{};
        Snapshot post{};
        for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
            pre.counters[i] = UINT64_MAX;
            post.counters[i] = 0u;
        }
        const Snapshot d = post - pre;
        for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
            CHECK(d.counters[i] == 0u);
        }
    }

    {
        const Snapshot empty_a{};
        const Snapshot empty_b{};
        const Snapshot d = empty_a - empty_b;
        for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
            CHECK(d.counters[i] == 0u);
        }
    }

    {
        Snapshot s{};
        s.counters[::crucible::perf::SCHED_CTX_VOL] = 12345u;
        s.counters[::crucible::perf::MEM_PAGE_FAULTS_MIN] = 67u;
        CHECK(s[::crucible::perf::SCHED_CTX_VOL] == 12345u);
        CHECK(s[::crucible::perf::MEM_PAGE_FAULTS_MIN] == 67u);
    }
}

// The array clobber stops the optimizer eliding writes into a buffer a
// benchmark has filled.  That it succeeds cannot be asserted from here,
// because the property is the absence of an elision; the blocking comes
// from an attribute on the empty function body, and the alternative of
// an inline-asm memory clobber was dropped because that idiom miscompiles
// under this compiler.
//
// What the tests below can hold the function to is the rest of its
// contract: callable on every element type and container a benchmark
// would pass, noexcept, and leaving the contents exactly as they were.

void test_clobber_array_noexcept() {
    using std::is_same_v;

    int arr_int[4] = {1, 2, 3, 4};
    static_assert(noexcept(bench::clobber_array(std::span<int>{arr_int})));

    int const arr_const[4] = {1, 2, 3, 4};
    static_assert(noexcept(bench::clobber_array(std::span<int const>{arr_const})));

    static_assert(is_same_v<decltype(bench::clobber_array(std::span<int>{arr_int})), void>);
    static_assert(is_same_v<decltype(bench::clobber_array(std::span<int const>{arr_const})), void>);
}

void test_clobber_array_does_not_mutate() {
    std::array<int, 5> data{10, 20, 30, 40, 50};
    bench::clobber_array(std::span<int>{data});
    CHECK(data[0] == 10);
    CHECK(data[1] == 20);
    CHECK(data[2] == 30);
    CHECK(data[3] == 40);
    CHECK(data[4] == 50);

    // The same data through the const overload.
    bench::clobber_array(std::span<int const>{data});
    CHECK(data[0] == 10);
    CHECK(data[4] == 50);
}

void test_clobber_array_various_types() {
    // These three doubles are exact in binary, so a change of any kind
    // would show even through the tolerance comparator.
    std::vector<double> dbls{1.5, -2.25, 3.125};
    bench::clobber_array(std::span<double>{dbls});
    CHECK(approx(dbls[0], 1.5));
    CHECK(approx(dbls[1], -2.25));
    CHECK(approx(dbls[2], 3.125));

    struct Pair {
        int a;
        double b;
    };
    Pair pairs[3] = {{1, 1.5}, {2, 2.5}, {3, 3.5}};
    bench::clobber_array(std::span<Pair>{pairs});
    CHECK(pairs[0].a == 1);
    CHECK(approx(pairs[2].b, 3.5));

    // The element type a benchmark's timestamp buffer actually uses.
    std::array<std::uint64_t, 8> cycles{};
    for (std::size_t i = 0; i < cycles.size(); ++i)
        cycles[i] = i * 100;
    bench::clobber_array(std::span<std::uint64_t>{cycles});
    for (std::size_t i = 0; i < cycles.size(); ++i) {
        CHECK(cycles[i] == i * 100);
    }
}

void test_clobber_array_empty_and_single() {
    // A zero-length array is not permitted, so the empty span is built
    // as a length-zero view over a one-element one.
    int empty_arr[1] = {};
    std::span<int> empty{empty_arr, 0};
    CHECK(empty.empty());
    bench::clobber_array(empty);
    CHECK(empty.empty());

    int one[1] = {42};
    bench::clobber_array(std::span<int>{one});
    CHECK(one[0] == 42);
}

void test_clobber_array_source_container_diversity() {
    {
        std::array<int, 3> a{7, 8, 9};
        bench::clobber_array(std::span<int>{a});
        CHECK(a[1] == 8);
    }

    {
        std::vector<int> v{100, 200, 300, 400};
        bench::clobber_array(std::span<int>{v});
        CHECK(v.size() == 4);
        CHECK(v[2] == 300);
    }

    {
        constexpr std::size_t N = 16;
        auto* heap = new int[N];
        for (std::size_t i = 0; i < N; ++i)
            heap[i] = static_cast<int>(i);
        bench::clobber_array(std::span<int>{heap, N});
        CHECK(heap[5] == 5);
        CHECK(heap[15] == 15);
        delete[] heap;
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_bench_harness: start\n");

    test_percentile_interp();
    test_percentiles_compute();
    test_bootstrap_ci();
    test_compare();
    test_snapshot_subtract();
    test_clobber_array_noexcept();
    test_clobber_array_does_not_mutate();
    test_clobber_array_various_types();
    test_clobber_array_empty_and_single();
    test_clobber_array_source_container_diversity();

    constexpr int kNumGroups = 10;
    if (g_failures == 0) {
        std::fprintf(stderr, "test_bench_harness: PASS (%d groups, 0 failures)\n", kNumGroups);
        return 0;
    }
    std::fprintf(stderr, "test_bench_harness: FAIL (%d groups, %d CHECK failure(s))\n", kNumGroups, g_failures);
    return 1;
}
