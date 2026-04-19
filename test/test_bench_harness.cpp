// Unit tests for bench/bench_harness.h statistical primitives and for
// bench/bpf_senses.h Snapshot delta.
//
// Coverage:
//   1. bench::percentile_interp        — R type 7 (Hyndman & Fan 1996)
//   2. bench::Percentiles::compute     — sort + per-percentile + moments
//   3. bench::bootstrap_ci             — Efron (1979) bootstrap CI
//   4. bench::compare                  — Mann-Whitney U (Mann & Whitney 1947)
//   5. bench::bpf::Snapshot::operator- — saturating per-counter delta
//
// Methodology:
//   Raw main() + CHECK macro. No gtest, no catch2. Every failure prints
//   file:line to stderr but the test continues so we see all failures
//   per run. main() returns the total failure count (nonzero → CTest
//   marks FAILED).
//
// Build context:
//   Default preset (GCC 16, -std=c++26 -Werror). CRUCIBLE_BENCH_HAVE_BPF
//   is NOT defined here → bench_harness.h's BPF-gated code paths are
//   inactive, and bpf_senses.h is included directly for Snapshot only
//   (we never instantiate SenseHub, which lives in bpf_senses.cpp and
//   is not linked).

#include "bench_harness.h"
#include "bpf_senses.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int g_failures = 0;

// One-line failure reporter. Ternary-safe; side-effect only on failure.
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "[FAIL] %s:%d  CHECK(%s)\n",                               \
                __FILE__, __LINE__, #cond);                                \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// Float-approx comparator. eps is absolute (values here are O(1)..O(100)).
[[nodiscard]] bool approx(double a, double b, double eps = 1e-9) noexcept {
    return std::abs(a - b) <= eps;
}

// ────────────────────────────────────────────────────────────────────
// 1. percentile_interp — R type 7 linear interpolation
// ────────────────────────────────────────────────────────────────────
//
// Type 7 (Hyndman & Fan 1996, R default) straddles two samples at
// frac * (n - 1). Defined tests:
//   • empty → 0 (graceful NDEBUG fallback)
//   • single element → that element at any frac
//   • odd-length symmetric set → exact sample values at quartiles
//   • even-length → midpoint interpolation between adjacent samples
void test_percentile_interp() {
    // ── empty → 0 ──
    // Note: in Debug builds the internal assert fires before the 0
    // return. We only exercise this in NDEBUG builds.
#ifdef NDEBUG
    {
        const std::vector<double> empty;
        CHECK(bench::percentile_interp(empty, 0.5) == 0.0);
    }
#endif

    // ── single element → that element ──
    {
        const std::vector<double> one{42.5};
        CHECK(approx(bench::percentile_interp(one, 0.0), 42.5));
        CHECK(approx(bench::percentile_interp(one, 0.5), 42.5));
        CHECK(approx(bench::percentile_interp(one, 1.0), 42.5));
    }

    // ── odd n: {1..5} — exact sample values at quartiles ──
    {
        const std::vector<double> x{1.0, 2.0, 3.0, 4.0, 5.0};
        CHECK(approx(bench::percentile_interp(x, 0.00), 1.0));
        CHECK(approx(bench::percentile_interp(x, 0.25), 2.0));
        CHECK(approx(bench::percentile_interp(x, 0.50), 3.0));
        CHECK(approx(bench::percentile_interp(x, 0.75), 4.0));
        CHECK(approx(bench::percentile_interp(x, 1.00), 5.0));
    }

    // ── even n: {1,2,3,4} at 0.5 — halfway between sorted[1] and sorted[2] ──
    {
        const std::vector<double> x{1.0, 2.0, 3.0, 4.0};
        CHECK(approx(bench::percentile_interp(x, 0.5), 2.5));
    }
}

// ────────────────────────────────────────────────────────────────────
// 2. Percentiles::compute — aggregate statistics over sorted samples
// ────────────────────────────────────────────────────────────────────
void test_percentiles_compute() {
    // ── empty input → all zero, n = 0 ──
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

    // ── uniform {5, 5, 5, 5, 5} → all percentiles = 5, stddev = 0 ──
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

    // ── symmetric {1..5}: mean = 3, stddev = √2.5 ≈ 1.5811 ──
    // Variance with Bessel correction: (4+1+0+1+4)/4 = 2.5.
    {
        std::vector<double> sym{3.0, 1.0, 4.0, 5.0, 2.0};   // deliberately unsorted
        const bench::Percentiles p = bench::Percentiles::compute(sym);
        CHECK(p.n == 5);
        CHECK(approx(p.min, 1.0));                   // min == sorted[0]
        CHECK(approx(p.max, 5.0));                   // max == sorted[n-1]
        CHECK(approx(p.mean, 3.0));
        CHECK(approx(p.p50, 3.0));
        CHECK(approx(p.stddev, std::sqrt(2.5), 1e-9));
        // cv = stddev / mean = √2.5 / 3
        CHECK(approx(p.cv, std::sqrt(2.5) / 3.0, 1e-9));
    }
}

// ────────────────────────────────────────────────────────────────────
// 3. bootstrap_ci — Efron (1979) with fixed seed for determinism
// ────────────────────────────────────────────────────────────────────
// Bit-exact double equality. We use this where deterministic RNG output
// is expected: two identical runs MUST produce the same bit pattern.
// Dodges -Wfloat-equal (which warns on direct `==` on doubles).
[[nodiscard]] bool bit_same(double a, double b) noexcept {
    return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);
}

// Dispatch the optional<CI> vs bare-CI API shape via a generic lambda.
// A generic lambda's body is a template; only the branch that matches
// the deduced parameter type is instantiated, so the discarded branch
// never needs to compile against the wrong shape.
//
// small-sample form: n < 30 → must be "empty" under either shape.
template <typename CI_t>
void check_ci_is_empty(const CI_t& ci) {
    if constexpr (requires { ci.value(); }) {
        // std::optional<CI>
        CHECK(!ci.has_value());
    } else {
        // bare CI — zero-initialised members signal "no CI".
        CHECK(bit_same(ci.lo, 0.0));
        CHECK(bit_same(ci.hi, 0.0));
    }
}

// determinism form: two runs with same seed → bit-identical bounds.
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
        CHECK(a.lo > 0.0);     // median of {1..100} is 50.5; CI brackets it
        CHECK(a.hi >= a.lo);
    }
}

void test_bootstrap_ci() {
    // ── n < 30 → Agent 1 may have promoted the return to optional<CI>.
    //    The helpers above cover both shapes via a templated probe.
    {
        std::vector<double> small;
        for (int i = 1; i <= 10; ++i) small.push_back(static_cast<double>(i));
        auto result = bench::bootstrap_ci(small, 0.5, /*B=*/100);
        check_ci_is_empty(result);
    }

    // ── determinism: same input + same seed → bit-identical output ──
    // Picks B=1000, seed = 0xBEEFCAFEDEADF00D (the bench_harness default).
    // We don't hard-code numeric bounds — the resampling distribution is
    // RNG-implementation-dependent. We only assert *stability* between
    // two independent runs on the same input.
    {
        std::vector<double> big;
        big.reserve(100);
        for (int i = 1; i <= 100; ++i) big.push_back(static_cast<double>(i));

        auto a = bench::bootstrap_ci(big, 0.5, /*B=*/1000, /*alpha=*/0.05,
                                     /*seed=*/0xBEEFCAFEDEADF00Dull);
        auto b = bench::bootstrap_ci(big, 0.5, /*B=*/1000, /*alpha=*/0.05,
                                     /*seed=*/0xBEEFCAFEDEADF00Dull);
        check_ci_stable(a, b);
    }
}

// ────────────────────────────────────────────────────────────────────
// 4. compare — Mann-Whitney U (Mann & Whitney 1947)
// ────────────────────────────────────────────────────────────────────
//
// Build two Reports with pre-populated .samples / .pct fields. No need
// to run the full Run::measure pipeline — compare reads only those.
[[nodiscard]] bench::Report make_report_from(
    const char* name,
    std::vector<double> samples) {
    bench::Report r;
    r.name    = name;
    r.samples = samples;                       // keep for compare
    r.pct     = bench::Percentiles::compute(samples);
    return r;
}

void test_compare() {
    // ── identical distributions {1..100} → indistinguishable, Δp50 = 0 ──
    {
        std::vector<double> v;
        v.reserve(100);
        for (int i = 1; i <= 100; ++i) v.push_back(static_cast<double>(i));

        const bench::Report a = make_report_from("a", v);
        const bench::Report b = make_report_from("b", v);
        const bench::Compare c = bench::compare(a, b);

        CHECK(!c.distinguishable);
        CHECK(approx(c.delta_p50_pct, 0.0, 1e-9));
        CHECK(approx(c.delta_p99_pct, 0.0, 1e-9));
        CHECK(approx(c.delta_mean_pct, 0.0, 1e-9));
    }

    // ── tiny uniform shift (+0.01) → Δp50 > 0, z finite ──
    // We intentionally do NOT assert distinguishability: whether a
    // +0.01 shift crosses |z| > 2.576 depends on the MW U variance
    // formula in effect (pre-fix lacks tie adjustment; post-fix adds
    // continuity correction). Either way Δp50 is strictly positive
    // and z is a finite number.
    {
        std::vector<double> va, vb;
        va.reserve(100);
        vb.reserve(100);
        for (int i = 1; i <= 100; ++i) {
            va.push_back(static_cast<double>(i));
            vb.push_back(static_cast<double>(i) + 0.01);
        }
        const bench::Report a = make_report_from("a", va);
        const bench::Report b = make_report_from("b+shift", vb);
        const bench::Compare c = bench::compare(a, b);

        CHECK(c.delta_p50_pct > 0.0);
        CHECK(std::isfinite(c.z));
        CHECK(std::isfinite(c.u));
    }

    // ── insufficient samples (< 30) → not distinguishable ──
    {
        std::vector<double> small_a, small_b;
        for (int i = 0; i < 20; ++i) {
            small_a.push_back(static_cast<double>(i));
            small_b.push_back(static_cast<double>(i) + 100.0);  // massive shift
        }
        const bench::Report a = make_report_from("small_a", small_a);
        const bench::Report b = make_report_from("small_b", small_b);
        const bench::Compare c = bench::compare(a, b);

        CHECK(!c.distinguishable);
    }

    // ── heavy ties: 50 copies of 1.0 + 50 copies of 2.0, in BOTH arms ──
    // Under any correct MW U variance (with or without tie adjustment),
    // two identical bimodal distributions must have z = 0 exactly.
    // Rank-sum symmetry ⇒ u1 = u2 = n1·n2/2 = μ, so the numerator
    // (u_min − μ) is zero regardless of σ.
    {
        std::vector<double> tie_a, tie_b;
        tie_a.reserve(100);
        tie_b.reserve(100);
        for (int i = 0; i < 50; ++i) {
            tie_a.push_back(1.0); tie_a.push_back(2.0);
            tie_b.push_back(1.0); tie_b.push_back(2.0);
        }
        const bench::Report a = make_report_from("tie_a", tie_a);
        const bench::Report b = make_report_from("tie_b", tie_b);
        const bench::Compare c = bench::compare(a, b);

        CHECK(!c.distinguishable);
        CHECK(std::abs(c.z) < 2.576);       // tie-adjusted OR raw: z ≈ 0
        CHECK(approx(c.z, 0.0, 1e-9));
    }
}

// ────────────────────────────────────────────────────────────────────
// 5. Snapshot::operator- — saturating per-counter subtraction
// ────────────────────────────────────────────────────────────────────
//
// Monotonic counters (SCHED_CTX_*, NET_TX_BYTES, …) produce meaningful
// post − pre. Gauges (FD_CURRENT, TCP_*, THERMAL_MAX_TRIP) can have
// post < pre; a raw u64 subtract would wrap to ~2^64. Snapshot uses
// crucible::sat::sub_sat and so MUST clamp to 0 on underflow.
void test_snapshot_subtract() {
    using bench::bpf::Snapshot;
    using bench::bpf::NUM_COUNTERS;

    // ── post > pre at every index → element-wise difference ──
    {
        Snapshot pre{};
        Snapshot post{};
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            pre.counters[i]  = static_cast<uint64_t>(i) * 10u;
            post.counters[i] = pre.counters[i] + static_cast<uint64_t>(i) + 1u;
        }
        const Snapshot d = post - pre;
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            CHECK(d.counters[i] == static_cast<uint64_t>(i) + 1u);
        }
    }

    // ── post < pre at ONE index → saturates to 0 at that index only ──
    // Every other slot subtracts normally (clamp only the underflowing
    // ones; don't zero the whole vector).
    {
        Snapshot pre{};
        Snapshot post{};
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            pre.counters[i]  = 100u;
            post.counters[i] = 150u;
        }
        constexpr size_t k = 17;                // arbitrary mid-range slot
        pre.counters[k]  = 200u;                // gauge that decreased
        post.counters[k] = 50u;                 // post < pre by 150

        const Snapshot d = post - pre;
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            if (i == k) {
                // Must clamp to 0 — NOT wrap to UINT64_MAX - 149.
                CHECK(d.counters[i] == 0u);
            } else {
                CHECK(d.counters[i] == 50u);
            }
        }
    }

    // ── pre > post at EVERY index, including extreme underflow ──
    {
        Snapshot pre{};
        Snapshot post{};
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            pre.counters[i]  = UINT64_MAX;       // max possible decrease
            post.counters[i] = 0u;
        }
        const Snapshot d = post - pre;
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            CHECK(d.counters[i] == 0u);
        }
    }

    // ── zero − zero = zero (baseline sanity) ──
    {
        const Snapshot empty_a{};
        const Snapshot empty_b{};
        const Snapshot d = empty_a - empty_b;
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            CHECK(d.counters[i] == 0u);
        }
    }

    // ── operator[] returns the counters[] entry ──
    {
        Snapshot s{};
        s.counters[bench::bpf::SCHED_CTX_VOL]        = 12345u;
        s.counters[bench::bpf::MEM_PAGE_FAULTS_MIN] = 67u;
        CHECK(s[bench::bpf::SCHED_CTX_VOL] == 12345u);
        CHECK(s[bench::bpf::MEM_PAGE_FAULTS_MIN] == 67u);
    }
}

} // namespace

int main() {
    std::fprintf(stderr, "test_bench_harness: start\n");

    test_percentile_interp();
    test_percentiles_compute();
    test_bootstrap_ci();
    test_compare();
    test_snapshot_subtract();

    if (g_failures == 0) {
        std::fprintf(stderr, "test_bench_harness: 5 groups PASSED\n");
        return 0;
    }
    std::fprintf(stderr,
        "test_bench_harness: %d CHECK failure(s)\n", g_failures);
    return 1;
}
