// The statistical primitives the benchmark harness reports through, the
// saturating counter delta the performance snapshot exposes, and the
// affinity that a Run sets on the calling thread.
//
// Each primitive follows a published definition, and the expected values
// below come from those definitions rather than from the current
// implementation:
//
//   percentile interpolation follows R type 7 (Hyndman and Fan, 1996)
//   the confidence interval follows Efron's bootstrap (1979)
//   the comparison follows the Mann-Whitney U test (1947)
//
// The affinity checks call Run::measure, so the harness tries to load the
// BPF sense hub when the crucible target defines CRUCIBLE_HAVE_BPF.  A host
// without the capabilities prints a warning, and the checks do not read
// the hub.

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

#include <sched.h>

#include "bench_harness.h"
#include <crucible/perf/SenseHub.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Sched.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
// The samples of a run of count samples, each the value that make gives its
// position.
template <class Make>
[[nodiscard]] ::fixy::HeapArray<double> run_of(std::size_t count, Make make) {
    return ::fixy::mint_heap_array_with<double>(::foundation::effects::testing::test().alloc, count, make);
}

// The ramp 1, 2, ..., kSampleN.
[[nodiscard]] ::fixy::View<const double> ramp_1_to_N() {
    static const ::fixy::HeapArray<double> ramp =
        run_of(kSampleN, [](std::size_t position) noexcept { return static_cast<double>(position + 1); });
    return ::fixy::View<const double>{ramp.view()};
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

// A copy of the values as a run of samples, the type that the percentiles and
// the bootstrap read.
[[nodiscard]] ::fixy::HeapArray<double> samples_of(::fixy::View<const double> values) {
    return run_of(values.size(), [&values](std::size_t position) noexcept {
        return *values.get(position).expect("a position below the count of the values");
    });
}

// The percentile frac of sorted values.
[[nodiscard]] double interp_of(::fixy::View<const double> sorted, double frac) {
    return bench::percentile_interp(sorted, frac);
}

// The percentiles of the values.
[[nodiscard]] bench::Percentiles percentiles_of(::fixy::View<const double> values) {
    ::fixy::HeapArray<double> samples = samples_of(values);
    return bench::Percentiles::compute(samples);
}

// The bootstrap interval of the percentile frac of the values.
[[nodiscard]] bench::CI bootstrap_of(::fixy::View<const double> values, double frac, std::size_t resamples,
                                     double alpha = kAlpha, std::uint64_t seed = kDefaultSeed) {
    return bench::bootstrap_ci(values, frac, resamples, alpha, seed);
}

// The comparison reads only the samples and the percentiles, so a report
// can be assembled directly instead of running a measurement.
[[nodiscard]] bench::Report make_report_from(::fixy::TextView name, ::fixy::View<const double> samples) {
    bench::Report r;
    r.name = bench::name_of(name);
    r.samples = samples_of(samples);
    r.pct = bench::Percentiles::compute(r.samples);
    return r;
}

// The values of a fixed array as a View of the samples.
template <std::size_t Count>
[[nodiscard]] ::fixy::View<const double> view_of(::fixy::FixedArray<double, Count> const& values) {
    return ::fixy::View<const double>{values.view()};
}

void test_percentile_interp() {
    // An empty input gives zero.
    CHECK(approx(interp_of(::fixy::View<const double>{}, 0.5), 0.0));

    {
        const ::fixy::FixedArray<double, 1> one{42.5};
        CHECK(approx(interp_of(view_of(one), 0.0), 42.5));
        CHECK(approx(interp_of(view_of(one), 0.5), 42.5));
        CHECK(approx(interp_of(view_of(one), 1.0), 42.5));
    }

    // With five samples every quartile lands on a sample, so these
    // expectations involve no interpolation at all.
    {
        const ::fixy::FixedArray<double, 5> x{1.0, 2.0, 3.0, 4.0, 5.0};
        CHECK(approx(interp_of(view_of(x), 0.00), 1.0));
        CHECK(approx(interp_of(view_of(x), 0.25), 2.0));
        CHECK(approx(interp_of(view_of(x), 0.50), 3.0));
        CHECK(approx(interp_of(view_of(x), 0.75), 4.0));
        CHECK(approx(interp_of(view_of(x), 1.00), 5.0));
    }

    // With four samples the median falls between two of them, which is
    // where the interpolation rule actually shows.
    {
        const ::fixy::FixedArray<double, 4> x{1.0, 2.0, 3.0, 4.0};
        CHECK(approx(interp_of(view_of(x), 0.5), 2.5));
    }
}

void test_percentiles_compute() {
    {
        const bench::Percentiles p = percentiles_of(::fixy::View<const double>{});
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
        const ::fixy::FixedArray<double, 5> uniform{5.0, 5.0, 5.0, 5.0, 5.0};
        const bench::Percentiles p = percentiles_of(view_of(uniform));
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
        const ::fixy::FixedArray<double, 5> sym{3.0, 1.0, 4.0, 5.0, 2.0};
        const bench::Percentiles p = percentiles_of(view_of(sym));
        CHECK(p.n == 5);
        CHECK(approx(p.min, 1.0));
        CHECK(approx(p.max, 5.0));
        CHECK(approx(p.mean, 3.0));
        CHECK(approx(p.p50, 3.0));
        CHECK(approx(p.stddev, std::sqrt(2.5), 1e-9));
        CHECK(approx(p.cv, std::sqrt(2.5) / 3.0, 1e-9));
    }
}

// The bounds depend on the draws of the Philox stream, so the exact
// interval is not pinned.  The same seed reproduces the same bounds bit for
// bit, and the interval of the median of the ramp 1 to 100 holds its median
// 50.5.
void test_bootstrap_ci() {
    {
        const ::fixy::HeapArray<double> small =
            run_of(kSmallN, [](std::size_t position) noexcept { return static_cast<double>(position + 1); });
        auto result = bootstrap_of(::fixy::View<const double>{small.view()}, 0.5, /*resamples=*/100);
        check_ci_is_empty(result);
    }

    {
        const ::fixy::View<const double> big = ramp_1_to_N();
        auto a = bootstrap_of(big, 0.5, kBootstrapB, kAlpha, kDefaultSeed);
        auto b = bootstrap_of(big, 0.5, kBootstrapB, kAlpha, kDefaultSeed);
        check_ci_stable(a, b);
        CHECK(a.lo <= 50.5 && a.hi >= 50.5);
    }
}

void test_compare() {
    {
        const ::fixy::View<const double> v = ramp_1_to_N();
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
        const ::fixy::HeapArray<double> va =
            run_of(kSampleN, [](std::size_t position) noexcept { return static_cast<double>(position + 1); });
        const ::fixy::HeapArray<double> vb =
            run_of(kSampleN, [](std::size_t position) noexcept { return static_cast<double>(position + 1) + 0.01; });
        const bench::Report a = make_report_from("a", ::fixy::View<const double>{va.view()});
        const bench::Report b = make_report_from("b+shift", ::fixy::View<const double>{vb.view()});
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
        const ::fixy::HeapArray<double> small_a =
            run_of(kSmallCmpN, [](std::size_t position) noexcept { return static_cast<double>(position); });
        const ::fixy::HeapArray<double> small_b =
            run_of(kSmallCmpN, [](std::size_t position) noexcept { return static_cast<double>(position) + 100.0; });
        const bench::Report a = make_report_from("small_a", ::fixy::View<const double>{small_a.view()});
        const bench::Report b = make_report_from("small_b", ::fixy::View<const double>{small_b.view()});
        const bench::Compare c = bench::compare(a, b);

        CHECK(!c.distinguishable);
    }

    // Both arms hold the same two values in equal numbers, which is the
    // worst case for ranking.  Rank-sum symmetry makes the two U values
    // equal to their common mean, so the statistic's numerator is zero
    // and the result is zero no matter which variance formula divides
    // it.
    {
        const auto one_or_two = [](std::size_t position) noexcept { return position % 2 == 0 ? 1.0 : 2.0; };
        const ::fixy::HeapArray<double> tie_a = run_of(kSampleN, one_or_two);
        const ::fixy::HeapArray<double> tie_b = run_of(kSampleN, one_or_two);
        const bench::Report a = make_report_from("tie_a", ::fixy::View<const double>{tie_a.view()});
        const bench::Report b = make_report_from("tie_b", ::fixy::View<const double>{tie_b.view()});
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
    using Slot = ::fixy::Index<NUM_COUNTERS>;
    using Slots = ::fixy::IndexRange<NUM_COUNTERS>;

    {
        Snapshot pre{};
        Snapshot post{};
        for (Slot const i : Slots{}) {
            std::uint64_t const before = i.raw() * 10u;
            pre.counters.with_element(i, [before](std::uint64_t& counter) noexcept { counter = before; });
            std::uint64_t const after = pre.counters.at(i) + i.raw() + 1u;
            post.counters.with_element(i, [after](std::uint64_t& counter) noexcept { counter = after; });
        }
        const Snapshot d = post - pre;
        for (Slot const i : Slots{}) {
            CHECK(d.counters.at(i) == i.raw() + 1u);
        }
    }

    // Exactly one slot underflows, so the neighbours are what proves the
    // clamp is per-slot.
    {
        Snapshot pre{};
        Snapshot post{};
        for (Slot const i : Slots{}) {
            pre.counters.with_element(i, [](std::uint64_t& counter) noexcept { counter = 100u; });
            post.counters.with_element(i, [](std::uint64_t& counter) noexcept { counter = 150u; });
        }
        // A slot in the middle rather than at either end, so that an
        // off-by-one in the clamp cannot be mistaken for correct.
        constexpr Slot k = ::fixy::index<17>();
        pre.counters.with_element(k, [](std::uint64_t& counter) noexcept { counter = 200u; });
        post.counters.with_element(k, [](std::uint64_t& counter) noexcept { counter = 50u; });

        const Snapshot d = post - pre;
        for (Slot const i : Slots{}) {
            if (i == k) {
                CHECK(d.counters.at(i) == 0u);
            } else {
                CHECK(d.counters.at(i) == 50u);
            }
        }
    }

    // The largest underflow representable, at every slot at once.
    {
        Snapshot pre{};
        Snapshot post{};
        for (Slot const i : Slots{}) {
            pre.counters.with_element(i, [](std::uint64_t& counter) noexcept { counter = UINT64_MAX; });
            post.counters.with_element(i, [](std::uint64_t& counter) noexcept { counter = 0u; });
        }
        const Snapshot d = post - pre;
        for (Slot const i : Slots{}) {
            CHECK(d.counters.at(i) == 0u);
        }
    }

    {
        const Snapshot empty_a{};
        const Snapshot empty_b{};
        const Snapshot d = empty_a - empty_b;
        for (Slot const i : Slots{}) {
            CHECK(d.counters.at(i) == 0u);
        }
    }

    {
        Snapshot s{};
        s.counters.with_element(::fixy::index<::crucible::perf::SCHED_CTX_VOL>(),
                                [](std::uint64_t& counter) noexcept { counter = 12345u; });
        s.counters.with_element(::fixy::index<::crucible::perf::MEM_PAGE_FAULTS_MIN>(),
                                [](std::uint64_t& counter) noexcept { counter = 67u; });
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

    ::fixy::FixedArray<int, 4> arr_int{1, 2, 3, 4};
    static_assert(noexcept(bench::clobber_array(arr_int.view())));

    ::fixy::FixedArray<int, 4> const arr_const{1, 2, 3, 4};
    static_assert(noexcept(bench::clobber_array(arr_const.view())));

    static_assert(is_same_v<decltype(bench::clobber_array(arr_int.view())), void>);
    static_assert(is_same_v<decltype(bench::clobber_array(arr_const.view())), void>);
}

void test_clobber_array_does_not_mutate() {
    ::fixy::FixedArray<int, 5> data{10, 20, 30, 40, 50};
    bench::clobber_array(data.view());
    CHECK(data.at(::fixy::index<0>()) == 10);
    CHECK(data.at(::fixy::index<1>()) == 20);
    CHECK(data.at(::fixy::index<2>()) == 30);
    CHECK(data.at(::fixy::index<3>()) == 40);
    CHECK(data.at(::fixy::index<4>()) == 50);

    // The same data through a View of const elements.
    bench::clobber_array(::fixy::View<int const>{data.view()});
    CHECK(data.at(::fixy::index<0>()) == 10);
    CHECK(data.at(::fixy::index<4>()) == 50);
}

void test_clobber_array_various_types() {
    // These three doubles are exact in binary, so a change of any kind
    // would show even through the tolerance comparator.
    ::fixy::FixedArray<double, 3> dbls{1.5, -2.25, 3.125};
    dbls.with_mut([](auto cells) noexcept { bench::clobber_array(::fixy::View<double>{cells.view()}); });
    CHECK(approx(dbls.at(::fixy::index<0>()), 1.5));
    CHECK(approx(dbls.at(::fixy::index<1>()), -2.25));
    CHECK(approx(dbls.at(::fixy::index<2>()), 3.125));

    struct Pair {
        int a = 0;
        double b = 0.0;
    };
    ::fixy::FixedArray<Pair, 3> pairs{Pair{1, 1.5}, Pair{2, 2.5}, Pair{3, 3.5}};
    bench::clobber_array(pairs.view());
    CHECK(pairs.at(::fixy::index<0>()).a == 1);
    CHECK(approx(pairs.at(::fixy::index<2>()).b, 3.5));

    // The element type a benchmark's timestamp buffer actually uses.
    ::fixy::FixedArray<std::uint64_t, 8> cycles{};
    for (::fixy::Index<8> const index : cycles.indices()) {
        std::uint64_t const value = index.raw() * 100;
        cycles.with_element(index, [value](std::uint64_t& cycle) noexcept { cycle = value; });
    }
    bench::clobber_array(cycles.view());
    for (::fixy::Index<8> const index : cycles.indices()) {
        CHECK(cycles.at(index) == index.raw() * 100);
    }
}

void test_clobber_array_empty_and_single() {
    // A zero-length array is not permitted, so the empty View is built
    // as a window of length zero over a one-element one.
    ::fixy::FixedArray<int, 1> empty_arr{};
    empty_arr.with_mut([](auto cells) noexcept {
        ::fixy::View<int> const empty = ::fixy::View<int>{cells.view()}.window(0, 0).expect("an empty window");
        CHECK(empty.size() == 0);
        bench::clobber_array(empty);
        CHECK(empty.size() == 0);
    });

    ::fixy::FixedArray<int, 1> one{42};
    bench::clobber_array(one.view());
    CHECK(one.at(::fixy::index<0>()) == 42);
}

void test_clobber_array_source_container_diversity() {
    {
        ::fixy::FixedArray<int, 3> a{7, 8, 9};
        bench::clobber_array(a.view());
        CHECK(a.at(::fixy::index<1>()) == 8);
    }

    {
        ::fixy::HeapArray<int> v = ::fixy::mint_heap_array_with<int>(
            ::foundation::effects::testing::test().alloc, 4,
            [](std::size_t position) noexcept { return static_cast<int>(position + 1) * 100; });
        bench::clobber_array(v.view());
        CHECK(v.size() == 4);
        CHECK(*v.get(2).expect("a position below the count") == 300);
    }

    {
        constexpr std::size_t N = 16;
        ::fixy::HeapArray<int> heap =
            ::fixy::mint_heap_array_with<int>(::foundation::effects::testing::test().alloc, N,
                                              [](std::size_t position) noexcept { return static_cast<int>(position); });
        bench::clobber_array(::fixy::View<int const>{heap.view()});
        CHECK(*heap.get(5).expect("a position below the count") == 5);
        CHECK(*heap.get(15).expect("a position below the count") == 15);
    }
}

// The sink of a timed body (THE SINK of bench/bench_harness.h).  An integer,
// an enumeration and a pointer take a general register, a float and a double
// a vector register, a small class whose copy is a copy of its bytes a
// general register or memory, and each other type memory.  Each form changes
// no bit of its value, the const form and the form that is not const.  That
// the optimizer keeps the computation of a value is the absence of an
// elision, so a run cannot show it.  The commit of the sink gives the
// disassembly of such a loop.
enum class SinkProbe : std::uint8_t {
    First = 3,
    Second = 7,
};

struct ThreeBytes {
    std::uint8_t first = 1;
    std::uint8_t second = 2;
    std::uint8_t third = 3;
};

struct TwoWords {
    std::uint32_t low = 5;
    std::uint32_t high = 9;
};

void test_sink_keeps_each_value() {
    using bench::SinkOperand;
    static_assert(bench::sink_operand_of<std::uint64_t> == SinkOperand::GeneralRegister);
    static_assert(bench::sink_operand_of<bool> == SinkOperand::GeneralRegister);
    static_assert(bench::sink_operand_of<SinkProbe> == SinkOperand::GeneralRegister);
    static_assert(bench::sink_operand_of<int const*> == SinkOperand::GeneralRegister);
    static_assert(bench::sink_operand_of<float> == SinkOperand::VectorRegister);
    static_assert(bench::sink_operand_of<double> == SinkOperand::VectorRegister);
    static_assert(bench::sink_operand_of<TwoWords> == SinkOperand::RegisterOrMemory);
    static_assert(bench::sink_operand_of<long double> == SinkOperand::Memory);
    static_assert(bench::sink_operand_of<ThreeBytes> == SinkOperand::Memory);
    static_assert(bench::sink_operand_of<::fixy::FixedArray<std::uint64_t, 64>> == SinkOperand::Memory);

    ::fixy::check::Tally checks{kTest, "test_sink_keeps_each_value"};
    std::uint64_t word = 0x0123456789ABCDEFULL;
    static_assert(noexcept(bench::do_not_optimize(word)));
    static_assert(std::is_same_v<decltype(bench::do_not_optimize(word)), void>);
    bench::do_not_optimize(word);
    bench::do_not_optimize(word + 1);
    CRUCIBLE_TEST_EXPECT(checks, word == 0x0123456789ABCDEFULL);

    SinkProbe probe = SinkProbe::Second;
    bench::do_not_optimize(probe);
    CRUCIBLE_TEST_EXPECT(checks, probe == SinkProbe::Second);

    TwoWords pair{};
    bench::do_not_optimize(pair);
    bench::do_not_optimize(TwoWords{});
    CRUCIBLE_TEST_EXPECT(checks, pair.low == 5 && pair.high == 9);

    // The bits of a float and of a double, so the compare is exact.
    float ratio = 0.375f;
    double scale = -2.25;
    bench::do_not_optimize(ratio);
    bench::do_not_optimize(scale);
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::bit_cast<std::uint32_t>(ratio) == ::fixy::bit_cast<std::uint32_t>(0.375f));
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::bit_cast<std::uint64_t>(scale) == ::fixy::bit_cast<std::uint64_t>(-2.25));

    ThreeBytes three{};
    bench::do_not_optimize(three);
    bench::do_not_optimize(ThreeBytes{});
    CRUCIBLE_TEST_EXPECT(checks, three.first == 1 && three.second == 2 && three.third == 3);

    ::fixy::FixedArray<std::uint64_t, 64> words{};
    for (::fixy::Index<64> const index : words.indices())
        words.with_element(index, [index](std::uint64_t& element) noexcept { element = index.raw() * 3; });
    bench::do_not_optimize(words);
    ::fixy::FixedArray<std::uint64_t, 64> const& read = words;
    bench::do_not_optimize(read);
    for (::fixy::Index<64> const index : words.indices()) {
        CRUCIBLE_TEST_EXPECT(checks, words.at(index) == index.raw() * 3);
    }

    // A loop that gives its sum to the form that is not const computes each
    // sum, so the last sum is the sum of each step.
    std::uint64_t sum = 0;
    for (std::uint64_t step = 0; step < 1000; ++step) {
        sum += step;
        bench::do_not_optimize(sum);
    }
    CRUCIBLE_TEST_EXPECT(checks, sum == 499500u);
    std::move(checks).close();
}

// A row whose median call takes less than kCycleFloor cycles of the TSC is
// NOT VALID (THE CYCLE FLOOR of bench/bench_harness.h).  One cycle a call is
// the time of a body with one load, so the floor does not mark it.  A report
// with no calibration of the TSC has no cycles, and the floor does not judge
// it.  The time of a real body changes with the load of the host, so each
// report here states its cycles.  The probe of test/bench_windows compiles a
// body that loses its work, and its check reads the timed loop.  The input
// sink of an object in memory changes no bit of the object.
void test_cycle_floor_marks_a_row() {
    ::fixy::check::Tally checks{kTest, "test_cycle_floor_marks_a_row"};
    bench::Report lost_work{};
    lost_work.p50_cycles = 0.49;
    CRUCIBLE_TEST_EXPECT(checks, lost_work.is_below_cycle_floor());
    bench::Report one_load{};
    one_load.p50_cycles = 0.99;
    CRUCIBLE_TEST_EXPECT(checks, !one_load.is_below_cycle_floor());
    bench::Report at_the_floor{};
    at_the_floor.p50_cycles = bench::kCycleFloor;
    CRUCIBLE_TEST_EXPECT(checks, !at_the_floor.is_below_cycle_floor());
    bench::Report not_calibrated{};
    CRUCIBLE_TEST_EXPECT(checks, !not_calibrated.is_below_cycle_floor());

    std::uint64_t cell = 0x5A5A5A5A5A5A5A5AULL;
    static_assert(noexcept(bench::mark_changed(cell)));
    bench::mark_changed(cell);
    CRUCIBLE_TEST_EXPECT(checks, cell == 0x5A5A5A5A5A5A5A5AULL);
    TwoWords pair{};
    bench::mark_changed(pair);
    CRUCIBLE_TEST_EXPECT(checks, pair.low == 5 && pair.high == 9);
    std::move(checks).close();
}

// A row through measure_with holds its state for the run, calls the body on
// it once for each iteration, and gives the state back beside the report.
// The count of the calls is the warmup and the timed iterations, so it is at
// least the count of the samples.
void test_measure_with_gives_the_state_back() {
    ::fixy::check::Tally checks{kTest, "test_measure_with_gives_the_state_back"};
    auto [report, calls] = bench::measure_with(bench::Run("state").samples(8).warmup(2), std::uint64_t{0},
                                               [](std::uint64_t& count) noexcept {
                                                   ++count;
                                                   bench::mark_changed(count);
                                               });
    CRUCIBLE_TEST_EXPECT(checks, calls >= 10);
    CRUCIBLE_TEST_EXPECT(checks, report.pct.p50 >= 0.0);
    std::move(checks).close();
}

// The number of CPUs in the affinity of the calling thread, or zero when
// the kernel refuses the query.
std::size_t affinity_cpu_count() {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof(set), &set) != 0) return 0;
    return static_cast<std::size_t>(CPU_COUNT(&set));
}

// The affinity count that a worker thread started from the calling thread
// sees.  A new thread inherits the affinity of the thread that starts it.
std::size_t worker_affinity_cpu_count() {
    std::size_t worker_count = 0;
    std::jthread worker{[&worker_count] { worker_count = affinity_cpu_count(); }};
    worker.join();
    return worker_count;
}

// The body of a Run runs under the pin to one CPU, and the thread gets back
// its affinity when the measurement ends.  The pin goes through the gated
// door of fixy/os/Sched.h, which records a pin event, so a CpuPinned proof
// of an earlier pin on the thread stops being in force.  The proof check
// runs on a thread of its own, so the pin of the proof to CPU 0 does not
// stay on the thread of the other checks.  A cpuset without CPU 0 refuses
// the proof, and that check then does nothing.
void test_run_pins_through_the_door_and_restores() {
    const std::size_t unpinned_count = affinity_cpu_count();
    std::size_t body_cpu_count = 0;
    const bench::Report pinned = bench::Run("one cpu").samples(8).warmup(1).measure(
        [&body_cpu_count]() noexcept { body_cpu_count = affinity_cpu_count(); });
    if (pinned.pinned_cpu.is_valid()) CHECK(body_cpu_count == 1u);
    CHECK(affinity_cpu_count() == unpinned_count);

    bool was_proof_checked = false;
    bool is_proof_in_force = true;
    std::jthread proof_thread{[&was_proof_checked, &is_proof_in_force]() noexcept {
        const ::fixy::BgLoadCtx background{::foundation::effects::testing::bg()};
        auto proof = ::fixy::sched::mint_affinity<::foundation::algebra::lattices::AffinityMask::single(0)>(background);
        if (proof.is_err()) return;
        const bench::Report report = bench::Run("pin event").samples(8).warmup(1).measure([]() noexcept {});
        if (!report.pinned_cpu.is_valid()) return;
        was_proof_checked = true;
        is_proof_in_force = std::move(proof).expect("the test above found the pin").is_in_force();
    }};
    proof_thread.join();
    if (was_proof_checked) CHECK(!is_proof_in_force);
}

// Run::fan_out starts the measuring thread on a CPU that isolcpus did not
// isolate, and keeps the affinity of the thread, so a worker that the body
// starts can use each CPU of that affinity.  Under the default pin each
// worker gets the one CPU of the pin.  A pin from pin_calling_thread holds
// until restore() gives the affinity back.
void test_fan_out_keeps_the_worker_affinity() {
    constexpr ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    const crucible::warden::CpuSet allowed = crucible::warden::allowed_cpus(startup);
    const crucible::warden::CpuSet isolated = crucible::warden::isolated_cpus(startup);
    std::size_t free_cpus = 0;
    for (const int cpu : allowed) {
        if (!isolated.contains(cpu)) ++free_cpus;
    }
    const std::size_t unpinned_count = affinity_cpu_count();

    std::size_t fanned_worker_count = 0;
    const bench::Report fanned = bench::Run("fan out").fan_out().samples(8).warmup(1).measure(
        [&fanned_worker_count]() noexcept { fanned_worker_count = worker_affinity_cpu_count(); });
    if (free_cpus > 0) {
        CHECK(fanned.pinned_cpu_count == free_cpus);
        CHECK(!fanned.pinned_cpu.is_valid());
        CHECK(fanned_worker_count == unpinned_count);
    } else {
        // A claim of loop bench: each allowed CPU is isolated, so the measuring
        // thread takes the core of slot 0, and the count is the count of the
        // cores of the claim.
        const bench::Cores claim = bench::Cores::of_calling_thread();
        CHECK(fanned.pinned_cpu_count == claim.count());
        CHECK(fanned.pinned_cpu == claim.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none()));
    }
    CHECK(affinity_cpu_count() == unpinned_count);

    std::size_t pinned_worker_count = 0;
    const bench::Report pinned = bench::Run("one cpu").samples(8).warmup(1).measure(
        [&pinned_worker_count]() noexcept { pinned_worker_count = worker_affinity_cpu_count(); });
    if (pinned.pinned_cpu.is_valid()) {
        CHECK(pinned.pinned_cpu_count == 1u);
        CHECK(pinned_worker_count == 1u);
    }

    bench::Run::PinResult held = bench::Run("held pin").pin_calling_thread();
    if (held.cpu.is_valid()) {
        CHECK(affinity_cpu_count() == 1u);
        CHECK(held.restore().is_ok());
    }
    CHECK(affinity_cpu_count() == unpinned_count);
}

// The core of a run stays in the affinity of the process.  Two benches that
// loop bench gives the cores 224 and 226 measure on those two cores, also when
// CRUCIBLE_BENCH_CORE names a third core, and a core that the affinity holds
// is taken as asked.  The masks are planted, so the test reads no affinity of
// the host.
// Runs a body on a thread of its own, through the joined thread door of the
// base, and joins the thread.  The join orders each write of the body before
// the return, so the body can borrow the locals of its caller.
template <class Body>
void run_on_own_thread(Body body) {
    namespace eff = ::foundation::effects;
    using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;
    ::fixy::thread::run_joined(Starter{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, std::move(body))
        .expect("the thread door starts a thread with the default options");
}

// The cores of a planted run: the isolated CPUs of the affinity, in the order
// of a planted order of loop bench.
[[nodiscard]] bench::Cores planted_cores_in(crucible::warden::CpuSet const& allowed,
                                            crucible::warden::CpuSet const& isolated, bench::CoreOrder const& order) {
    return bench::Cores::of(::fixy::side_as<bench::side::AllowedCpus>(allowed),
                            ::fixy::side_as<bench::side::IsolatedCpus>(isolated), order);
}

// The cores of a planted run with no order of loop bench, in ascending order.
[[nodiscard]] bench::Cores planted_cores(crucible::warden::CpuSet const& allowed,
                                         crucible::warden::CpuSet const& isolated) {
    return planted_cores_in(allowed, isolated, bench::CoreOrder{});
}

// The isolated CPUs of the bench VM: the even CPUs 224 to 254.
[[nodiscard]] crucible::warden::CpuSet planted_isolated() {
    crucible::warden::CpuSet isolated{};
    for (std::size_t cpu = 224; cpu < 256; cpu += 2)
        isolated.insert(cpu);
    return isolated;
}

[[nodiscard]] int core_of_run(crucible::warden::CpuSet const& allowed, int asked) {
    return bench::detail::core_of_run(planted_cores(allowed, planted_isolated()), allowed, bench::CpuId{asked}).raw();
}

void test_core_of_run_stays_in_the_affinity() {
    crucible::warden::CpuSet first{};
    first.insert(224);
    crucible::warden::CpuSet second{};
    second.insert(226);
    CHECK(core_of_run(first, -1) == 224);
    CHECK(core_of_run(second, -1) == 226);
    CHECK(core_of_run(first, 246) == 224);
    CHECK(core_of_run(second, 246) == 226);
    CHECK(core_of_run(first, 246) != core_of_run(second, 246));
    crucible::warden::CpuSet both{};
    both.insert(224);
    both.insert(226);
    CHECK(core_of_run(both, 226) == 226);
    CHECK(core_of_run(crucible::warden::CpuSet{}, 224) == -1);
}

// A claim of loop bench of four cores, 240 to 246, as the affinity of the
// process.  The measuring thread takes the first core, each slot takes its own
// core, and no slot past the claim gets one.  Two claims at the same time give
// no core to two threads.  An affinity with no isolated CPU, as under ctest,
// gives no core to any slot.
void test_slots_take_distinct_cores() {
    crucible::warden::CpuSet claim{};
    for (std::size_t cpu = 240; cpu <= 246; cpu += 2)
        claim.insert(cpu);
    const bench::Cores cores = planted_cores(claim, planted_isolated());
    CHECK(cores.count() == 4u);
    CHECK(core_of_run(claim, -1) == 240);
    const auto core_of_slot = [&cores](std::uint32_t slot) noexcept {
        return cores.of_slot(bench::ThreadSlot{slot}).value_or(bench::CpuId::none()).raw();
    };
    CHECK(core_of_slot(0) == 240 && core_of_slot(1) == 242 && core_of_slot(2) == 244 && core_of_slot(3) == 246);
    CHECK(core_of_slot(4) == -1);

    crucible::warden::CpuSet other{};
    other.insert(224);
    other.insert(226);
    const bench::Cores second = planted_cores(other, planted_isolated());
    for (std::uint32_t slot = 0; slot < 2; ++slot) {
        CHECK(!claim.contains(second.of_slot(bench::ThreadSlot{slot}).value_or(bench::CpuId::none()).raw()));
    }

    crucible::warden::CpuSet shared{};
    shared.insert_range(0, 223);
    const bench::Cores none = planted_cores(shared, planted_isolated());
    CHECK(none.count() == 0u);
    CHECK(none.of_slot(bench::kMeasuringSlot).is_none());
}

// The order of a text of loop bench, or an empty order with a failed check
// when the parse refuses the text.
[[nodiscard]] bench::CoreOrder planted_order(::fixy::TextView text) {
    ::fixy::Option<bench::CoreOrder> parsed = bench::parse_core_order(text);
    CHECK(parsed.is_some());
    return std::move(parsed).value_or(bench::CoreOrder{});
}

// The CPU of a slot of a planted run, or -1 past the last slot.
[[nodiscard]] int core_at(bench::Cores const& cores, bench::ThreadSlot slot) noexcept {
    return cores.of_slot(slot).value_or(bench::CpuId::none()).raw();
}

// A claim of loop bench of the cores 240 to 246, with the order "246,240,244"
// of the steal record.  Slot 0, the measuring thread, takes cpu246, the first
// core of the order, and the core of the run with no asked core is cpu246.
// The slots then take 240 and 244, and the core that the order does not name,
// 242, comes last.  An order that names a CPU outside the claim skips it.  A
// text with no number, a number that parse() refuses, a CPU past the last CPU
// of a mask, or a CPU twice is refused.  An empty text is an empty order.
void test_slots_follow_the_order_of_the_claim() {
    using Slot = bench::ThreadSlot;
    crucible::warden::CpuSet claim{};
    for (std::size_t cpu = 240; cpu <= 246; cpu += 2)
        claim.insert(cpu);
    const bench::Cores ordered = planted_cores_in(claim, planted_isolated(), planted_order("246,240,244"));
    CHECK(ordered.count() == 4u && ordered.source() == bench::SlotOrder::Claim);
    CHECK(core_at(ordered, bench::kMeasuringSlot) == 246);
    CHECK(core_at(ordered, Slot{1}) == 240 && core_at(ordered, Slot{2}) == 244 && core_at(ordered, Slot{3}) == 242);
    CHECK(core_at(ordered, Slot{4}) == -1);
    CHECK(bench::detail::core_of_run(ordered, claim, bench::CpuId::none()).raw() == 246);
    const crucible::warden::CpuSet later = ordered.from_slot(Slot{1});
    CHECK(later.size() == 3u && !later.contains(246) && later.contains(242));
    const bench::Spread spread = bench::Spread::of_claim(ordered);
    CHECK(spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none()).raw() == 246);
    CHECK(spread.of_slot(Slot{3}).value_or(bench::CpuId::none()).raw() == 242);

    const bench::Cores outside = planted_cores_in(claim, planted_isolated(), planted_order("250,242"));
    CHECK(outside.source() == bench::SlotOrder::Claim && core_at(outside, bench::kMeasuringSlot) == 242);
    CHECK(core_at(outside, Slot{1}) == 240 && core_at(outside, Slot{3}) == 246 && outside.count() == 4u);

    const bench::Cores ascending = planted_cores(claim, planted_isolated());
    CHECK(ascending.source() == bench::SlotOrder::Ascending && core_at(ascending, bench::kMeasuringSlot) == 240);

    CHECK(planted_order("").size() == 0u);
    CHECK(planted_order("7").size() == 1u);
    CHECK(bench::parse_core_order("240,,242").is_none());
    CHECK(bench::parse_core_order("240,").is_none());
    CHECK(bench::parse_core_order(",240").is_none());
    CHECK(bench::parse_core_order("240;242").is_none());
    CHECK(bench::parse_core_order(" 240").is_none());
    CHECK(bench::parse_core_order("240,240").is_none());
    CHECK(bench::parse_core_order("1024").is_none());
    CHECK(bench::parse_core_order("-1").is_none());
}

// The CPU of a slot of a spread, or -1 past the last slot.
[[nodiscard]] int cpu_at(bench::Spread const& spread, bench::ThreadSlot slot) noexcept {
    return spread.of_slot(slot).value_or(bench::CpuId::none()).raw();
}

// The spread of a run with no claim takes one CPU of each core first, then
// the SMT siblings, and the core of cpu0 last in each rank.  The shared CPUs
// of luks-a are 0 to 223, with the siblings 2k and 2k + 1: a run of 32
// threads takes the even CPUs 2 to 64, a run of 112 threads takes each core
// with cpu0 last, and slot 112 takes cpu3.  The bench host has 384 CPUs, with
// the sibling of CPU c at c + 192: slots 0 to 191 take the cores of the two
// sockets, and a run of 224 threads takes 32 siblings last.  A CPU whose
// lower sibling is not in the affinity is the first CPU of its core.  A host
// whose sysfs gives no group spreads in ascending order.  A claim gives its
// cores in ascending order.
void test_spread_of_a_run() {
    using Slot = bench::ThreadSlot;
    const auto pairs = [](bench::CpuId cpu) noexcept {
        crucible::warden::CpuSet group{};
        group.insert_range(static_cast<std::size_t>(cpu.raw() & ~1), static_cast<std::size_t>(cpu.raw() | 1));
        return group;
    };
    crucible::warden::CpuSet shared{};
    shared.insert_range(0, 223);
    const bench::Spread luks = bench::Spread::of_topology(shared, pairs);
    CHECK(luks.count() == 224u && luks.source() == bench::SpreadSource::Topology);
    CHECK(cpu_at(luks, Slot{0}) == 2 && cpu_at(luks, Slot{1}) == 4 && cpu_at(luks, Slot{31}) == 64);
    CHECK(cpu_at(luks, Slot{110}) == 222 && cpu_at(luks, Slot{111}) == 0 && cpu_at(luks, Slot{112}) == 3);
    CHECK(cpu_at(luks, Slot{222}) == 223 && cpu_at(luks, Slot{223}) == 1 && cpu_at(luks, Slot{224}) == -1);

    const auto sockets = [](bench::CpuId cpu) noexcept {
        crucible::warden::CpuSet group{};
        const auto core = static_cast<std::size_t>(cpu.raw() % 192);
        group.insert(core);
        group.insert(core + 192);
        return group;
    };
    crucible::warden::CpuSet host{};
    host.insert_range(0, 383);
    const bench::Spread bench_host = bench::Spread::of_topology(host, sockets);
    CHECK(bench_host.count() == 384u);
    CHECK(cpu_at(bench_host, Slot{0}) == 1 && cpu_at(bench_host, Slot{63}) == 64);
    CHECK(cpu_at(bench_host, Slot{190}) == 191 && cpu_at(bench_host, Slot{191}) == 0);
    CHECK(cpu_at(bench_host, Slot{192}) == 193 && cpu_at(bench_host, Slot{223}) == 224);
    CHECK(cpu_at(bench_host, Slot{383}) == 192);

    crucible::warden::CpuSet odd{};
    odd.insert(1);
    odd.insert(3);
    odd.insert(4);
    const bench::Spread partial = bench::Spread::of_topology(odd, pairs);
    CHECK(partial.count() == 3u && cpu_at(partial, Slot{0}) == 3 && cpu_at(partial, Slot{1}) == 4
          && cpu_at(partial, Slot{2}) == 1);

    const bench::Spread flat =
        bench::Spread::of_topology(odd, [](bench::CpuId) noexcept { return crucible::warden::CpuSet{}; });
    CHECK(cpu_at(flat, Slot{0}) == 1 && cpu_at(flat, Slot{2}) == 4);

    crucible::warden::CpuSet claim{};
    for (std::size_t cpu = 240; cpu <= 246; cpu += 2)
        claim.insert(cpu);
    const bench::Spread claimed = bench::Spread::of_claim(planted_cores(claim, planted_isolated()));
    CHECK(claimed.count() == 4u && claimed.source() == bench::SpreadSource::Claim);
    CHECK(cpu_at(claimed, Slot{0}) == 240 && cpu_at(claimed, Slot{3}) == 246 && cpu_at(claimed, Slot{4}) == -1);
}

// The measuring thread of a fan-out takes the core of slot 0 when each allowed
// CPU is isolated, a claim of loop bench, because no load balancer spreads the
// threads there.  An affinity with a CPU that is not isolated gives no core,
// and the threads spread over those CPUs.  The masks are planted.  The old
// fan-out gave no pin on a claim, so each thread of the body stayed on one
// core.
void test_fan_out_core_of_a_claim() {
    const auto fan_out_core_of = [](crucible::warden::CpuSet const& allowed, ::fixy::TextView order) noexcept {
        return bench::detail::fan_out_core(::fixy::side_as<bench::side::AllowedCpus>(allowed),
                                           ::fixy::side_as<bench::side::IsolatedCpus>(planted_isolated()),
                                           planted_order(order))
            .raw();
    };
    crucible::warden::CpuSet claim{};
    claim.insert(240);
    claim.insert(242);
    claim.insert(244);
    CHECK(fan_out_core_of(claim, "244,240,242") == 244);
    CHECK(fan_out_core_of(claim, "") == 240);
    crucible::warden::CpuSet mixed = claim;
    mixed.insert(2);
    CHECK(fan_out_core_of(mixed, "244,240,242") == -1);
    CHECK(fan_out_core_of(crucible::warden::CpuSet{}, "") == -1);
}

// A claim of nine cores of the bench VM spans its two L3 groups, the even CPUs
// 224 to 238 and 240 to 254.  In a claim of eight cores of one group and one
// core of the other, loop bench puts the lone core last in the order of the
// claim, the noisiest core.  The measuring thread of a row takes the core of
// slot 0, the quietest core, and not the lone core.  With no order of loop
// bench, slot 0 is the lowest core.  An empty claim gives none.  The shards of
// a row take the first cores of the order apart from the measuring core.
void test_measuring_core_of_a_claim() {
    const auto measuring_in = [](crucible::warden::CpuSet const& claim, ::fixy::TextView order) noexcept {
        return planted_cores_in(claim, planted_isolated(), planted_order(order))
            .of_slot(bench::kMeasuringSlot)
            .value_or(bench::CpuId::none())
            .raw();
    };
    crucible::warden::CpuSet eight_and_one{};
    eight_and_one.insert(224);
    for (std::size_t cpu = 240; cpu <= 254; cpu += 2)
        eight_and_one.insert(cpu);
    CHECK(measuring_in(eight_and_one, "242,240,244,246,248,250,252,254,224") == 242);
    CHECK(measuring_in(eight_and_one, "") == 224);
    crucible::warden::CpuSet one_group{};
    for (std::size_t cpu = 240; cpu <= 246; cpu += 2)
        one_group.insert(cpu);
    CHECK(measuring_in(one_group, "244,240,246,242") == 244);
    CHECK(measuring_in(crucible::warden::CpuSet{}, "") == -1);
    crucible::warden::CpuSet two_apart{};
    two_apart.insert(224);
    two_apart.insert(226);
    for (std::size_t cpu = 240; cpu <= 252; cpu += 2)
        two_apart.insert(cpu);

    using CoreCount = ::fixy::Quantity<::fixy::unit::Element, std::size_t>;
    const bench::Cores ordered = planted_cores_in(two_apart, planted_isolated(), planted_order("246,224,250,240"));
    const crucible::warden::CpuSet shards = ordered.first_slots_apart(bench::CpuId{224}, CoreCount{2});
    CHECK(shards.size() == 2u && shards.contains(246) && shards.contains(250) && !shards.contains(224));
    const crucible::warden::CpuSet all = ordered.first_slots_apart(bench::CpuId{224}, CoreCount{64});
    CHECK(all.size() == 8u && !all.contains(224));

    // The L3 group of cpu0 on the host holds cpu0, or sysfs gives no L3.
    const crucible::warden::CpuSet host_group = bench::read_l3_group(bench::CpuId{0});
    CHECK(host_group.is_empty() || host_group.contains(0));
    CHECK(bench::read_l3_group(bench::CpuId::none()).is_empty());
}

// The CPU of a thread is field 39 of its stat file, and the fields start after
// the last closing parenthesis, because the name of a thread can hold one.
void test_processor_of_stat() {
    const ::fixy::TextView stat =
        "77 (bad) name) S 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 "
        "29 30 31 32 33 34 35 36 37 38 241 40 41 42\n";
    CHECK(bench::detail::processor_of_stat(stat).value_or(0) == 241u);
    CHECK(bench::detail::processor_of_stat("no parenthesis 1 2 3").is_none());
}

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
// The top set of the off-CPU spikes of a run counts each event and keeps the
// five longest spikes, longest first, each with its CPU.  A spike that is
// not longer than the shortest kept one of a full set goes, and equal spikes
// keep the order of their arrival.
void test_offcpu_top_keeps_the_longest() {
    bench::detail::OffCpuTop top{};
    CHECK(top.kept == 0u && top.count == 0u);
    constexpr ::fixy::FixedArray<std::uint64_t, 8> kOffNs{40, 10, 90, 10, 70, 5, 90, 20};
    std::uint32_t cpu = 1;
    for (std::uint64_t const off_ns : kOffNs) {
        top.keep(bench::detail::OffCpuSpike{.off_ns = off_ns, .cpu = cpu});
        ++cpu;
    }
    CHECK(top.count == 8u && top.kept == 5u);
    constexpr ::fixy::FixedArray<std::uint64_t, 5> kLongest{90, 90, 70, 40, 20};
    constexpr ::fixy::FixedArray<std::uint32_t, 5> kCpus{3, 7, 5, 1, 8};
    for (auto const place : kLongest.indices()) {
        CHECK(top.longest.at(place).off_ns == kLongest.at(place));
        CHECK(top.longest.at(place).cpu == kCpus.at(place));
    }
}
#endif

// An environment text holds each variable as NAME=VALUE with a zero after
// it.  A variable is found by its whole name: a name that is a prefix of
// another name finds nothing, and a value can be empty or hold an equals
// sign.  The last variable needs no zero after it.
void test_environment_values() {
    ::fixy::FixedText<256> environment{};
    constexpr ::fixy::FixedArray<::fixy::TextView, 5> kVariables{
        ::fixy::TextView{"PATH=/usr/bin"}, ::fixy::TextView{"CRUCIBLE_BENCH_CORE=7"},
        ::fixy::TextView{"CRUCIBLE_BENCH_JSON="}, ::fixy::TextView{"CRUCIBLE_BENCH_HARDENING=a=b"},
        ::fixy::TextView{"CRUCIBLE_BENCH_SAMPLES=300"}};
    std::size_t written = 0;
    for (::fixy::TextView const variable : kVariables) {
        CHECK(environment.append(variable).is_ok());
        ++written;
        if (written < kVariables.size()) CHECK(environment.append('\0').is_ok());
    }
    const auto value_of = [&environment](::fixy::TextView name) noexcept {
        return bench::detail::value_in(environment.view(), name);
    };
    CHECK(value_of("CRUCIBLE_BENCH_CORE").value_or("none") == ::fixy::TextView{"7"});
    CHECK(value_of("CRUCIBLE_BENCH_JSON").value_or("none") == ::fixy::TextView{});
    CHECK(value_of("CRUCIBLE_BENCH_HARDENING").value_or("none") == ::fixy::TextView{"a=b"});
    CHECK(value_of("CRUCIBLE_BENCH_SAMPLES").value_or("none") == ::fixy::TextView{"300"});
    CHECK(value_of("PATH").value_or("none") == ::fixy::TextView{"/usr/bin"});
    CHECK(value_of("CRUCIBLE_BENCH").is_none());
    CHECK(value_of("CRUCIBLE_BENCH_CORE_ORDER").is_none());
    CHECK(value_of("USR").is_none());
    CHECK(bench::detail::value_in(::fixy::TextView{}, "PATH").is_none());
}

// The texts of the columns of a report.  A time takes two decimals in the
// largest unit in which it is at least 1, rounded as printf %.2f rounds the
// exact binary value, and a value whose digits do not fit writes nan.  A
// JSON name escapes each character that a JSON string refuses.  A name holds
// at most kNameBytes bytes, and a longer one ends the process.
void name_of_a_long_text() {
    constexpr ::fixy::TextView kLong{
        "a name of more than 128 bytes, which no row of a bench may take, because the column of the name and the "
        "reader of the JSON tail.."};
    static_cast<void>(bench::name_of(kLong));
}

void test_texts_of_reports() {
    // A text of the base, held while its view is compared.
    const auto is_text = [](auto const& text, ::fixy::TextView expected) noexcept { return text.view() == expected; };
    CHECK(is_text(bench::detail::ns_text(12.345), "12.35ns"));
    CHECK(is_text(bench::detail::ns_text(999.994), "999.99ns"));
    // The unit comes from the value before the rounding, as before.
    CHECK(is_text(bench::detail::ns_text(999.995), "1000.00ns"));
    CHECK(is_text(bench::detail::ns_text(0.0), "0.00ns"));
    CHECK(is_text(bench::detail::ns_text(1500.0), "1.50µs"));
    CHECK(is_text(bench::detail::ns_text(2.5e6), "2.50ms"));
    CHECK(is_text(bench::detail::ns_text(3.0e9), "3.00s"));
    CHECK(is_text(bench::detail::ns_text(1e40), "nans"));
    CHECK(is_text(bench::detail::json_name_of("plain"), "\"plain\""));
    CHECK(is_text(bench::detail::json_name_of("a\"b\\c\n\t\x01"), "\"a\\\"b\\\\c\\n\\t\\u0001\""));
    CHECK(is_text(bench::detail::json_name_of(""), "\"\""));
    CHECK(is_text(bench::format_name("{} P={}", ::fixy::TextView{"single"}, 4), "single P=4"));
    CHECK(is_text(bench::name_of("row"), "row"));
    ::fixy::check::expect_dies(kTest, name_of_a_long_text, ::fixy::TextView{"the name of a bench row holds 129 bytes"});
}

// A scaling_cur_freq file holds the frequency in kHz and a newline.  The
// frequency in Hz is that number times 1000, and a text with no number, a
// sign or an empty text gives 0, as for a CPU with no cpufreq directory.
void test_freq_of_text() {
    CHECK(bench::detail::freq_hz_of_text("3450000\n") == 3450000000u);
    CHECK(bench::detail::freq_hz_of_text("  800000\n") == 800000000u);
    CHECK(bench::detail::freq_hz_of_text("1") == 1000u);
    CHECK(bench::detail::freq_hz_of_text("") == 0u);
    CHECK(bench::detail::freq_hz_of_text("-5\n") == 0u);
    CHECK(bench::detail::freq_hz_of_text("unknown\n") == 0u);
    CHECK(bench::detail::read_cpu_freq_hz(bench::CpuId::none()) == 0u);
}

// A worker pins itself to the core of its slot, and a thread that starts
// inside run_on_core takes that core as its affinity and keeps it.  The
// calling thread gets its affinity back after run_on_core.  The core is the
// first CPU of the affinity of the test, so the checks run on each host.  A
// core that is not valid pins nothing.
void test_pins_of_the_threads_of_a_run() {
    const std::size_t unpinned_count = affinity_cpu_count();
    const int first_cpu = []() noexcept {
        constexpr ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        return crucible::warden::allowed_cpus(startup).first().value_or(-1);
    }();
    if (first_cpu < 0) return;
    std::size_t worker_count = 0;
    bool is_pinned = false;
    run_on_own_thread([&worker_count, &is_pinned, first_cpu]() noexcept {
        is_pinned = bench::pin_calling_thread_to(bench::CpuId{first_cpu}).is_ok();
        worker_count = affinity_cpu_count();
    });
    CHECK(is_pinned && worker_count == 1u);
    CHECK(bench::pin_calling_thread_to(bench::CpuId::none()).is_err());

    std::size_t started_count = 0;
    const std::size_t held_count = bench::run_on_core(bench::CpuId{first_cpu}, [&started_count]() noexcept {
        run_on_own_thread([&started_count]() noexcept { started_count = affinity_cpu_count(); });
        return affinity_cpu_count();
    });
    CHECK(held_count == 1u && started_count == 1u);
    CHECK(affinity_cpu_count() == unpinned_count);
    // The one read of the current CPU of the harness gives the core of the
    // hold.
    const bench::CpuId held_cpu =
        bench::run_on_core(bench::CpuId{first_cpu}, []() noexcept { return bench::detail::calling_cpu(); });
    CHECK(held_cpu == bench::CpuId{first_cpu});
    const std::size_t free_count =
        bench::run_on_core(bench::CpuId::none(), []() noexcept { return affinity_cpu_count(); });
    CHECK(free_count == unpinned_count);
}

// The median and the spread of the medians of the windows of a run, on
// planted medians.  An odd count gives the middle value, and an even count
// the mean of the two middle values, as percentile_interp does.  The spread
// is the largest median minus the smallest, as a part of the median.  Equal
// medians give no spread, and a median of zero gives no spread and no
// division.  The order of the medians changes nothing.
void test_spread_of_planted_windows() {
    const auto spread_of = [](auto const& medians) noexcept { return bench::spread_of_windows(medians.view()); };
    const bench::WindowSpread none = bench::spread_of_windows(::fixy::View<double const>{});
    CHECK(none.windows == 0u && approx(none.median, 0.0) && approx(none.spread, 0.0));

    const bench::WindowSpread one = spread_of(::fixy::FixedArray<double, 1>{10.0});
    CHECK(one.windows == 1u && approx(one.median, 10.0) && approx(one.spread, 0.0));

    const bench::WindowSpread five = spread_of(::fixy::FixedArray<double, 5>{12.0, 10.0, 11.0, 13.0, 14.0});
    CHECK(five.windows == 5u && approx(five.median, 12.0) && approx(five.spread, 4.0 / 12.0));

    const bench::WindowSpread even = spread_of(::fixy::FixedArray<double, 4>{4.0, 2.0, 8.0, 6.0});
    CHECK(even.windows == 4u && approx(even.median, 5.0) && approx(even.spread, 6.0 / 5.0));

    const bench::WindowSpread ties = spread_of(::fixy::FixedArray<double, 5>{3.0, 1.0, 3.0, 2.0, 3.0});
    CHECK(approx(ties.median, 3.0) && approx(ties.spread, 2.0 / 3.0));

    const bench::WindowSpread flat = spread_of(::fixy::FixedArray<double, 3>{5.0, 5.0, 5.0});
    CHECK(approx(flat.median, 5.0) && approx(flat.spread, 0.0));

    const bench::WindowSpread zero = spread_of(::fixy::FixedArray<double, 2>{0.0, 0.0});
    CHECK(zero.windows == 2u && approx(zero.median, 0.0) && approx(zero.spread, 0.0));

    const bench::WindowSpread reversed = spread_of(::fixy::FixedArray<double, 5>{14.0, 13.0, 12.0, 11.0, 10.0});
    CHECK(approx(reversed.median, five.median) && approx(reversed.spread, five.spread));
}

// The median of a part of a run, as the drift and the windows read it, on
// planted samples in the order of a loop.  An even part gives the mean of its
// two middle values, an odd part its middle value, a part of one sample that
// sample, and an empty part 0, as percentile_interp of a sorted part does.
void test_median_of_a_part() {
    ::fixy::FixedArray<double, 6> const samples{5.0, 1.0, 4.0, 2.0, 3.0, 9.0};
    const auto median_of = [&samples](std::size_t first, std::size_t end) noexcept {
        ::fixy::View<double const> const part =
            samples.view().window(first, end - first).expect("the part fits the planted samples");
        return bench::median_of_part(
            ::fixy::mint_heap_array_copy<double>(::foundation::effects::testing::test().alloc, part));
    };
    CHECK(approx(median_of(0, 6), 3.5));
    CHECK(approx(median_of(1, 4), 2.0));
    CHECK(approx(median_of(0, 1), 5.0));
    CHECK(approx(median_of(2, 2), 0.0));
    CHECK(approx(median_of(3, 6), 3.0));
}

// A Run splits its sampled loop into five windows (THE WINDOWS of
// bench/bench_harness.h).  Ten samples give five windows and keep the ten
// samples.  Three samples give one window for each sample.  A body of about
// a millisecond with a cap of 50 ms ends each window at its part of the time
// after its first 13 samples: the run keeps five windows and takes at least
// 65 and far fewer than its 1000 samples.  A run of 12 samples under a cap of
// 1 ms keeps each of its 12 samples.
void test_run_measures_in_windows() {
    const bench::Report ten = bench::Run("ten samples").samples(10).warmup(1).batch(1).measure([]() noexcept {});
    CHECK(ten.pct.n == 10u && ten.windows.windows == bench::kWindows);
    const bench::Report three = bench::Run("three samples").samples(3).warmup(1).batch(1).measure([]() noexcept {});
    CHECK(three.pct.n == 3u && three.windows.windows == 3u);

    constexpr std::uint64_t kSpinSteps = 2000000;
    const auto spin = []() noexcept {
        std::uint64_t sum = 0;
        for (std::uint64_t step = 0; step < kSpinSteps; ++step) {
            sum += step;
            bench::do_not_optimize(sum);
        }
    };
    const bench::Report capped = bench::Run("capped").samples(1000).warmup(1).batch(1).max_wall_ms(50).measure(spin);
    CHECK(capped.windows.windows == bench::kWindows);
    CHECK(capped.pct.n >= bench::kWindows * bench::kMinWindowSamples && capped.pct.n < 1000u);
    const bench::Report short_run =
        bench::Run("short capped").samples(12).warmup(1).batch(1).max_wall_ms(1).measure(spin);
    CHECK(short_run.pct.n == 12u && short_run.windows.windows == bench::kWindows);
}

// The windows judge their lost time only when the check of the whole loop
// judged the lost time of the measuring thread.  On planted lost times, the
// windows over the bound of 1 ms count under that basis, and none counts
// under each other basis.  A body that sleeps 2 ms in each sample loses that
// time in each window, but the measuring thread slept, so the check of the
// whole loop judges its run delay, and the windows judge nothing.
void test_windows_judge_no_sleep() {
    namespace eff = ::foundation::effects;
    const ::fixy::FixedArray<std::uint64_t, 3> lost{0, 2000000, 500000};
    CHECK(bench::detail::paused_windows_of(lost.view(), bench::PauseBasis::LostTime) == 1u);
    CHECK(bench::detail::paused_windows_of(lost.view(), bench::PauseBasis::RunDelay) == 0u);
    CHECK(bench::detail::paused_windows_of(lost.view(), bench::PauseBasis::NotMeasured) == 0u);

    constexpr std::uint64_t kSleepNs = 2000000;
    const ::fixy::InitLoadCtx startup{eff::testing::init()};
    const auto sleeper = ::fixy::time::mint_bounded_sleep<kSleepNs>(startup);
    const bench::Report slept = bench::Run("sleeps").samples(5).warmup(1).batch(1).measure(
        [&sleeper]() noexcept { static_cast<void>(sleeper.sleep_for(kSleepNs)); });
    CHECK(slept.pause.basis == bench::PauseBasis::RunDelay);
    CHECK(slept.window_basis == bench::PauseBasis::NotMeasured && slept.paused_windows == 0u);
}

// The thread 100 of the planted readings is the measuring thread, and the
// thread 7 is the other thread of the run.
constexpr ::fixy::process::ThreadId kMeasuringTid{100};
constexpr ::fixy::process::ThreadId kOtherTid{7};

// A planted reading of the measuring thread, which lists the measuring thread
// with its own run delay and the thread 7 with no run delay.
[[nodiscard]] bench::PauseReading with_threads(bench::PauseReading reading) {
    reading.basis = bench::PauseBasis::LostTime;
    reading.own.tid = kMeasuringTid;
    static_cast<void>(reading.others.append(reading.own));
    static_cast<void>(reading.others.append(bench::ThreadDelay{.tid = kOtherTid}));
    return reading;
}

// The planted reading after a window in which the thread 7 waited on its run
// queue in some arrivals.
[[nodiscard]] bench::PauseReading with_other_delay(bench::PauseReading reading, bench::ThreadDelay const& delay) {
    for (bench::ThreadDelay& thread : reading.others) {
        if (thread.tid == delay.tid) thread = delay;
    }
    if (delay.tid != kOtherTid && delay.tid != kMeasuringTid) static_cast<void>(reading.others.append(delay));
    return reading;
}

[[nodiscard]] bench::Pause planted_pause(::fixy::Side<bench::side::ReadingBefore, bench::PauseReading> const& before,
                                         ::fixy::Side<bench::side::ReadingAfter, bench::PauseReading> const& after) {
    return bench::detail::pause_between(before, after);
}

// The verdict of the pause check on planted readings.  A thread that never
// slept is judged by its lost time, a thread that slept by its run delay, and
// another thread by its run delay.  A run delay is a pause only with a mean
// wait of more than 100 us for each arrival.  A thread that only one reading
// holds, and the measuring thread in the list of the threads, count nothing.
// A reading that failed gives no pause.
void test_pause_between_planted_readings() {
    using bench::side::ReadingAfter;
    using bench::side::ReadingBefore;
    const bench::PauseReading start = with_threads({.wall_ns = 1000000, .cpu_ns = 500000, .voluntary_switches = 4});
    const bench::Pause stolen = planted_pause(
        ::fixy::side_as<ReadingBefore>(start),
        ::fixy::side_as<ReadingAfter>(with_threads({.wall_ns = 11000000, .cpu_ns = 8000000, .voluntary_switches = 4})));
    CHECK(stolen.basis == bench::PauseBasis::LostTime && stolen.wall_ns == 10000000u);
    CHECK(stolen.lost_ns == 2500000u && stolen.own_ns() == 2500000u && stolen.is_over_bound());

    const bench::PauseReading quiet = with_threads({.voluntary_switches = 4});
    const bench::Pause clean = planted_pause(
        ::fixy::side_as<ReadingBefore>(quiet),
        ::fixy::side_as<ReadingAfter>(with_threads({.wall_ns = 10000000, .cpu_ns = 9950000, .voluntary_switches = 4})));
    CHECK(clean.lost_ns == 50000u && !clean.is_over_bound());

    const bench::Pause slept = planted_pause(
        ::fixy::side_as<ReadingBefore>(with_threads({.voluntary_switches = 4, .own = {.run_delay_ns = 100}})),
        ::fixy::side_as<ReadingAfter>(with_threads(
            {.wall_ns = 10000000, .cpu_ns = 5000000, .voluntary_switches = 5, .own = {.run_delay_ns = 200100}})));
    CHECK(slept.basis == bench::PauseBasis::RunDelay && slept.own_ns() == 200000u && !slept.is_over_bound());

    // A thread that slept and then waited 2 ms in 4 arrivals lost its core.
    // The same 2 ms in 100 arrivals are the wakes of a parked thread.
    const bench::Pause shared = planted_pause(
        ::fixy::side_as<ReadingBefore>(quiet),
        ::fixy::side_as<ReadingAfter>(with_threads(
            {.wall_ns = 10000000, .voluntary_switches = 5, .own = {.run_delay_ns = 2000000, .arrivals = 4}})));
    CHECK(shared.basis == bench::PauseBasis::RunDelay && shared.is_over_bound());
    const bench::Pause parked = planted_pause(
        ::fixy::side_as<ReadingBefore>(quiet),
        ::fixy::side_as<ReadingAfter>(with_threads(
            {.wall_ns = 10000000, .voluntary_switches = 5, .own = {.run_delay_ns = 2000000, .arrivals = 100}})));
    CHECK(parked.own_ns() == 2000000u && !parked.is_over_bound());

    // The thread 7 waited 1.5 ms in 3 arrivals, and the thread 9 waited 5 ms in
    // 1000 arrivals.  The check names the thread 7, whose delay is a pause.  The
    // thread 8 is only in the later reading, so it counts nothing.
    const bench::PauseReading busy = with_threads({.wall_ns = 10000000, .cpu_ns = 10000000, .voluntary_switches = 4});
    const bench::PauseReading late = with_other_delay(
        with_other_delay(with_other_delay(busy, {.tid = kOtherTid, .run_delay_ns = 1500000, .arrivals = 3}),
                         {.tid = ::fixy::process::ThreadId{9}, .run_delay_ns = 5000000, .arrivals = 1000}),
        {.tid = ::fixy::process::ThreadId{8}, .run_delay_ns = 9000000, .arrivals = 1});
    const bench::PauseReading early = with_other_delay(quiet, {.tid = ::fixy::process::ThreadId{9}});
    const bench::Pause delayed =
        planted_pause(::fixy::side_as<ReadingBefore>(early), ::fixy::side_as<ReadingAfter>(late));
    CHECK(delayed.own_ns() == 0u && delayed.other.run_delay_ns == 1500000u && delayed.other.arrivals == 3u);
    CHECK(delayed.other.tid == kOtherTid && delayed.is_over_bound());

    // With no delay that is a pause, the check names the longest delay, and the
    // run stays valid.
    const bench::Pause woken =
        planted_pause(::fixy::side_as<ReadingBefore>(early),
                      ::fixy::side_as<ReadingAfter>(with_other_delay(
                          busy, {.tid = ::fixy::process::ThreadId{9}, .run_delay_ns = 5000000, .arrivals = 1000})));
    CHECK(woken.other.tid == ::fixy::process::ThreadId{9} && !woken.is_over_bound());

    const bench::Pause unread =
        planted_pause(::fixy::side_as<ReadingBefore>(bench::PauseReading{}),
                      ::fixy::side_as<ReadingAfter>(with_threads({.wall_ns = 10000000, .voluntary_switches = 4})));
    CHECK(unread.basis == bench::PauseBasis::NotMeasured && !unread.is_over_bound());
}

// A planted pause of 2 ms.  A hog thread starts before the measurement and
// sleeps.  In the sampled loop, the body asks for the hog and spins until it
// is done.  The hog pins itself to the CPU of the measuring thread and takes
// that CPU for 2 ms of its own CPU time, so the measuring thread cannot run
// for those 2 ms.  The steal column of /proc/stat counts ticks of 10 ms and
// reads such a pause as zero.  The pause check reports at least 1.9 ms of lost
// time and marks the run.  The test runs on loaded CPUs, so it checks only
// that the planted pause is caught.
void test_planted_pause_is_caught() {
    namespace eff = ::foundation::effects;
    using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
    constexpr std::uint64_t kHogNs = 2000000;
    constexpr std::uint64_t kPollNs = 50000;
    constexpr std::uint32_t kTriggerCall = 40;
    const int first_cpu = []() noexcept {
        constexpr ::fixy::InitLoadCtx startup{eff::testing::init()};
        return crucible::warden::allowed_cpus(startup).first().value_or(-1);
    }();
    if (first_cpu < 0) return;
    ::fixy::Atomic<bool> is_hog_asked{false};
    ::fixy::Atomic<bool> is_hog_pinned{false};
    ::fixy::Atomic<bool> is_hog_done{false};
    // The hog runs beside the measure, and it reaches the measure only
    // through the three atomic cells.
    auto hog = [&is_hog_asked, &is_hog_pinned, &is_hog_done, first_cpu]() noexcept {
        const ::fixy::InitLoadCtx startup{eff::testing::init()};
        const auto sleeper = ::fixy::time::mint_bounded_sleep<kPollNs>(startup);
        while (!is_hog_asked.load_acquire()) {
            static_cast<void>(sleeper.sleep_for(kPollNs));
        }
        is_hog_pinned.store_release(bench::pin_calling_thread_to(bench::CpuId{first_cpu}).is_ok());
        const auto cpu = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::ThreadCpu>(startup);
        const auto now = [&cpu]() noexcept {
            return cpu.read(eff::testing::test().nondet).expect("the read of the CPU time of the thread").peek();
        };
        const std::uint64_t start = now();
        while (now() - start < kHogNs) {}
        is_hog_done.store_release(true);
    };
    auto measure = [&is_hog_asked, &is_hog_done, first_cpu]() noexcept {
        std::uint32_t calls = 0;
        bench::Report report =
            bench::Run("planted pause").core(first_cpu).samples(200).warmup(1).batch(1).measure([&]() noexcept {
                ++calls;
                if (calls != kTriggerCall) return;
                is_hog_asked.store_release(true);
                while (!is_hog_done.load_acquire()) {
                    CRUCIBLE_SPIN_PAUSE;
                }
            });
        // A body that never asked for the hog lets it end here, so the join
        // after the measure ends.
        is_hog_asked.store_release(true);
        return report;
    };
    auto values = ::fixy::thread::run_beside(Starter{eff::testing::bg()}, ::fixy::thread::ThreadOptions{},
                                             std::move(hog), measure)
                      .expect("the thread door starts the thread of the planted pause");
    const bench::Report& paused = values.at(::fixy::index<1>());
    if (!paused.pinned_cpu.is_valid()) return;
    CHECK(is_hog_pinned.load_acquire() && is_hog_done.load_acquire());
    // The body spins and never sleeps, so the measuring thread makes no
    // voluntary switch in the window, and the check judges it by its lost
    // time.  The readings open no file between their clocks.  So the read-set
    // tracer of `loop test-step`, which stops each open, adds no switch and no
    // arrival in the window (THE PAUSE CHECK of bench/bench_harness.h).  The
    // first run of each new build of the test is a traced run.
    CHECK(paused.pause.basis == bench::PauseBasis::LostTime);
    CHECK(paused.pause.lost_ns >= 1900000u);
    CHECK(paused.pause.own_ns() >= 1900000u);
    CHECK(paused.pause.is_over_bound());
    // The window of the planted pause has its own check, and it marks that
    // window (THE WINDOWS of bench/bench_harness.h).
    CHECK(paused.paused_windows >= 1u);
    const ::fixy::TextView basis = paused.pause.basis == bench::PauseBasis::LostTime ? ::fixy::TextView{"lost time"}
                                                                                     : ::fixy::TextView{"run delay"};
    ::fixy::report(::fixy::Sink::Err,
                   "test_bench_harness: the planted pause of 2 ms lost {} us of {} us, with a run delay of {} us.  The "
                   "check judged it by its {}\n",
                   paused.pause.lost_ns / 1000, paused.pause.wall_ns / 1000, paused.pause.own.run_delay_ns / 1000,
                   basis);
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
    test_sink_keeps_each_value();
    test_cycle_floor_marks_a_row();
    test_measure_with_gives_the_state_back();
    test_run_pins_through_the_door_and_restores();
    test_fan_out_keeps_the_worker_affinity();
    test_core_of_run_stays_in_the_affinity();
    test_slots_take_distinct_cores();
    test_slots_follow_the_order_of_the_claim();
    test_spread_of_a_run();
    test_measuring_core_of_a_claim();
    test_fan_out_core_of_a_claim();
    test_pins_of_the_threads_of_a_run();
    test_pause_between_planted_readings();
    test_planted_pause_is_caught();
    test_processor_of_stat();
    test_environment_values();
    test_texts_of_reports();
    test_freq_of_text();
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    test_offcpu_top_keeps_the_longest();
#endif
    test_spread_of_planted_windows();
    test_median_of_a_part();
    test_run_measures_in_windows();
    test_windows_judge_no_sleep();

    constexpr int kNumGroups = 27;
    if (g_failures == 0) {
        ::fixy::check::pass(kTest, "test_bench_harness: PASS ({} groups, 0 failures)\n", kNumGroups);
        return 0;
    }
    std::fprintf(stderr, "test_bench_harness: FAIL (%d groups, %d CHECK failure(s))\n", kNumGroups, g_failures);
    return 1;
}
