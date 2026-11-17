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
#include "test_assert.h"
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
#include <fixy/Ctx.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/OwnedThread.h>
#include <fixy/os/Sched.h>
#include <foundation/algebra/lattices/AffinityLattice.h>

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
    using Slot = ::fixy::Index<NUM_COUNTERS>;
    using Slots = ::fixy::IndexRange<NUM_COUNTERS>;

    {
        Snapshot pre{};
        Snapshot post{};
        for (Slot const i : Slots{}) {
            pre.counters.at(i) = i.raw() * 10u;
            post.counters.at(i) = pre.counters.at(i) + i.raw() + 1u;
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
            pre.counters.at(i) = 100u;
            post.counters.at(i) = 150u;
        }
        // A slot in the middle rather than at either end, so that an
        // off-by-one in the clamp cannot be mistaken for correct.
        constexpr Slot k = ::fixy::index<17>();
        pre.counters.at(k) = 200u;
        post.counters.at(k) = 50u;

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
            pre.counters.at(i) = UINT64_MAX;
            post.counters.at(i) = 0u;
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
        s.counters.at(::fixy::index<::crucible::perf::SCHED_CTX_VOL>()) = 12345u;
        s.counters.at(::fixy::index<::crucible::perf::MEM_PAGE_FAULTS_MIN>()) = 67u;
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
        if (!proof.has_value()) return;
        const bench::Report report = bench::Run("pin event").samples(8).warmup(1).measure([]() noexcept {});
        if (!report.pinned_cpu.is_valid()) return;
        was_proof_checked = true;
        is_proof_in_force = proof->is_in_force();
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
    CHECK(fanned.pinned_cpu_count == free_cpus);
    if (free_cpus > 0) {
        CHECK(!fanned.pinned_cpu.is_valid());
        CHECK(fanned_worker_count == unpinned_count);
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
// Runs a body on a thread of its own, through the owned thread of the base,
// and joins the thread.  The join orders each write of the body before the
// return.
template <class Body>
void run_on_own_thread(Body body) {
    namespace eff = ::foundation::effects;
    using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
    std::move(
        ::fixy::thread::mint_owned_thread(Starter{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, std::move(body))
            .expect("the thread door starts a thread with the default options"))
        .join();
}

// The cores of a planted run: the isolated CPUs of the affinity.
[[nodiscard]] bench::Cores planted_cores(crucible::warden::CpuSet const& allowed,
                                         crucible::warden::CpuSet const& isolated) {
    return bench::Cores::of(::fixy::side_as<bench::side::AllowedCpus>(allowed),
                            ::fixy::side_as<bench::side::IsolatedCpus>(isolated));
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

// A claim of nine cores of the bench VM spans its two L3 groups, the even CPUs
// 224 to 238 and 240 to 254.  The core apart is the one core of the claim in
// its group, in either order of the groups.  A claim in one group gives its
// highest core.  An empty claim gives none.
void test_apart_core_of_a_claim() {
    const auto groups = [](bench::CpuId cpu) noexcept {
        crucible::warden::CpuSet group{};
        const std::size_t first = cpu.raw() < 240 ? 224 : 240;
        for (std::size_t member = first; member < first + 16; member += 2)
            group.insert(member);
        return group;
    };
    const auto apart_of = [&groups](crucible::warden::CpuSet const& claim) noexcept {
        return bench::apart_core(planted_cores(claim, planted_isolated()), groups).value_or(bench::CpuId::none()).raw();
    };
    crucible::warden::CpuSet low_apart{};
    low_apart.insert(224);
    for (std::size_t cpu = 240; cpu <= 254; cpu += 2)
        low_apart.insert(cpu);
    CHECK(apart_of(low_apart) == 224);
    crucible::warden::CpuSet high_apart{};
    for (std::size_t cpu = 224; cpu <= 238; cpu += 2)
        high_apart.insert(cpu);
    high_apart.insert(240);
    CHECK(apart_of(high_apart) == 240);
    crucible::warden::CpuSet one_group{};
    for (std::size_t cpu = 240; cpu <= 246; cpu += 2)
        one_group.insert(cpu);
    CHECK(apart_of(one_group) == 246);
    CHECK(apart_of(crucible::warden::CpuSet{}) == -1);

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
    const std::size_t free_count =
        bench::run_on_core(bench::CpuId::none(), []() noexcept { return affinity_cpu_count(); });
    CHECK(free_count == unpinned_count);
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
    static_cast<void>(reading.others.push(reading.own));
    static_cast<void>(reading.others.push(bench::ThreadDelay{.tid = kOtherTid}));
    return reading;
}

// The planted reading after a window in which the thread 7 waited on its run
// queue in some arrivals.
[[nodiscard]] bench::PauseReading with_other_delay(bench::PauseReading reading, bench::ThreadDelay const& delay) {
    for (bench::ThreadDelay& thread : reading.others) {
        if (thread.tid == delay.tid) thread = delay;
    }
    if (delay.tid != kOtherTid && delay.tid != kMeasuringTid) static_cast<void>(reading.others.push(delay));
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
    ::fixy::thread::OwnedThread hog =
        ::fixy::thread::mint_owned_thread(
            Starter{eff::testing::bg()}, ::fixy::thread::ThreadOptions{},
            [&is_hog_asked, &is_hog_pinned, &is_hog_done, first_cpu]() noexcept {
                const ::fixy::InitLoadCtx startup{eff::testing::init()};
                const auto sleeper = ::fixy::time::mint_bounded_sleep<kPollNs>(startup);
                while (!is_hog_asked.load_acquire()) {
                    static_cast<void>(sleeper.sleep_for(kPollNs));
                }
                is_hog_pinned.store_release(bench::pin_calling_thread_to(bench::CpuId{first_cpu}).is_ok());
                const auto cpu = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::ThreadCpu>(startup);
                const auto now = [&cpu]() noexcept {
                    return cpu.read(eff::testing::test().nondet)
                        .expect("the read of the CPU time of the thread")
                        .peek();
                };
                const std::uint64_t start = now();
                while (now() - start < kHogNs) {}
                is_hog_done.store_release(true);
            })
            .expect("the thread door starts the thread of the planted pause");
    std::uint32_t calls = 0;
    const bench::Report paused =
        bench::Run("planted pause").core(first_cpu).samples(200).warmup(1).batch(1).measure([&]() noexcept {
            ++calls;
            if (calls != kTriggerCall) return;
            is_hog_asked.store_release(true);
            while (!is_hog_done.load_acquire()) {
                CRUCIBLE_SPIN_PAUSE;
            }
        });
    // A body that never asked for the hog lets it end here, so the join ends.
    is_hog_asked.store_release(true);
    std::move(hog).join();
    if (!paused.pinned_cpu.is_valid()) return;
    CHECK(is_hog_pinned.load_acquire() && is_hog_done.load_acquire());
    // A launcher that counts instructions, as `loop test-step` does, gives the
    // measuring thread a voluntary switch in the window.  The check then
    // judges it by its run delay, which also holds the 2 ms.
    CHECK(paused.pause.lost_ns >= 1900000u);
    CHECK(paused.pause.own_ns() >= 1900000u);
    CHECK(paused.pause.is_over_bound());
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
    test_run_pins_through_the_door_and_restores();
    test_fan_out_keeps_the_worker_affinity();
    test_core_of_run_stays_in_the_affinity();
    test_slots_take_distinct_cores();
    test_apart_core_of_a_claim();
    test_pins_of_the_threads_of_a_run();
    test_pause_between_planted_readings();
    test_planted_pause_is_caught();
    test_processor_of_stat();

    constexpr int kNumGroups = 18;
    if (g_failures == 0) {
        crucible::test::pass("test_bench_harness: PASS ({} groups, 0 failures)\n", kNumGroups);
        return 0;
    }
    std::fprintf(stderr, "test_bench_harness: FAIL (%d groups, %d CHECK failure(s))\n", kNumGroups, g_failures);
    return 1;
}
