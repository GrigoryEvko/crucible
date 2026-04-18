#pragma once

// Minimal benchmark harness — no external dependencies.
//
// Usage:
//   BENCH("arena_alloc", 1'000'000, {
//       auto* p = arena.alloc(64);
//       DoNotOptimize(p);
//   });
//
// Prints: arena_alloc: 2.3 ns/op (1000000 iterations)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef __linux__
#include <sys/resource.h>
#include <sched.h>
#endif

namespace bench {

inline void elevate_priority() {
#ifdef __linux__
    setpriority(PRIO_PROCESS, 0, -10);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<size_t>(sched_getcpu()), &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
#endif
}

// Prevent compiler from optimizing away a value.
template <typename T>
inline void DoNotOptimize(T const& val) {
    asm volatile("" : : "r,m"(val) : "memory");
}

// Prevent compiler from reordering across this barrier.
inline void ClobberMemory() {
    asm volatile("" : : : "memory");
}

// Read TSC (Time Stamp Counter) — cycle-accurate on x86-64.
inline uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// Calibrate TSC → nanoseconds. Also elevates priority on first call.
inline double tsc_ns_ratio() {
    static bool once = (elevate_priority(), true);
    (void)once;

    using clock = std::chrono::high_resolution_clock;
    constexpr int CALIBRATION_MS = 50;

    auto t0 = clock::now();
    uint64_t c0 = rdtsc();

    // Busy-wait for calibration period.
    auto deadline = t0 + std::chrono::milliseconds(CALIBRATION_MS);
    while (clock::now() < deadline) {}

    uint64_t c1 = rdtsc();
    auto t1 = clock::now();

    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double cycles = static_cast<double>(c1 - c0);
    return ns / cycles;
}

struct Result {
    const char* name = nullptr;
    double ns_per_op = 0.0;
    double min_ns = 0.0;
    double max_ns = 0.0;
    double median_ns = 0.0;
    double p10_ns = 0.0;
    double p90_ns = 0.0;
    uint64_t iterations = 0;
};

// Run a benchmark function `iters` times, repeat `rounds` times,
// report min/median/max ns per operation.
template <typename Fn>
Result run(const char* name, uint64_t iters, uint32_t rounds, Fn&& fn) {
    static double ratio = tsc_ns_ratio();

    std::vector<double> samples;
    samples.reserve(rounds);

    for (uint32_t r = 0; r < rounds; r++) {
        ClobberMemory();
        uint64_t start = rdtsc();

        for (uint64_t i = 0; i < iters; i++) {
            fn();
        }

        ClobberMemory();
        uint64_t end = rdtsc();

        double total_ns = static_cast<double>(end - start) * ratio;
        samples.push_back(total_ns / static_cast<double>(iters));
    }

    std::sort(samples.begin(), samples.end());

    Result res{};
    res.name = name;
    res.ns_per_op = samples[rounds / 2];  // median
    res.min_ns = samples[0];
    res.max_ns = samples[rounds - 1];
    res.median_ns = samples[rounds / 2];
    res.p10_ns = samples[rounds / 10];
    res.p90_ns = samples[rounds - 1 - rounds / 10];
    res.iterations = iters;
    return res;
}

inline void print_result(const Result& r) {
    std::printf("  %-40s %6.1f ns/op  (min=%5.1f  med=%5.1f  max=%6.1f)  [%lu iters]\n",
                r.name, r.ns_per_op, r.min_ns, r.median_ns, r.max_ns, r.iterations);
}

// Abort if p90/p10 spread is too wide — means the system is under load
// and benchmark results are unreliable. Skip for manual Results where
// p10/p90 are 0 (not computed), and for sub-0.5ns operations where
// measurement noise dominates.
//
// Default 4.0× catches system load (Chrome, builds, YouTube, etc.).
// For benchmarks with known structural variance (burst fills that span
// L1→L2→L3 cache hierarchy), use BENCH_CHECK_V with a higher ratio.
inline void check_variance(const Result& r, double max_ratio = 4.0) {
    if (r.p10_ns < 0.1 || r.p90_ns < 0.1) return;  // not computed or sub-noise
    double ratio = r.p90_ns / r.p10_ns;
    if (ratio > max_ratio) {
        std::fprintf(stderr,
            "NOISY SYSTEM: %-40s  p90/p10=%.1fx (limit=%.1fx)  "
            "p10=%.1f ns  p90=%.1f ns — close other apps\n",
            r.name, ratio, max_ratio, r.p10_ns, r.p90_ns);
        std::abort();
    }
}

// Read CRUCIBLE_BENCH_MULT once (default 1.0).  Host-specific calibration
// lives in the env, not in per-bench source — a slower box can set 2.0
// without any source change.  Negative / non-numeric values fall back.
inline double bench_mult() {
    static const double mult = [] {
        const char* s = std::getenv("CRUCIBLE_BENCH_MULT");
        if (!s) return 1.0;
        char* end = nullptr;
        double v = std::strtod(s, &end);
        return (end != s && v > 0) ? v : 1.0;
    }();
    return mult;
}

// Check if median exceeds regression threshold × CRUCIBLE_BENCH_MULT.
// Aborts with a clear message — assert is disabled in release builds.
inline void check_regression(const Result& r, double max_ns,
                             double max_var_ratio = 4.0) {
    check_variance(r, max_var_ratio);
    const double eff = max_ns * bench_mult();
    if (r.median_ns > eff) {
        std::fprintf(stderr,
            "REGRESSION: %-40s  median=%.1f ns  threshold=%.1f ns"
            " (base=%.1f × CRUCIBLE_BENCH_MULT=%.2f)\n",
            r.name, r.median_ns, eff, max_ns, bench_mult());
        std::abort();
    }
}

// Convenience macro.
#define BENCH(name, iters, body)                                      \
    do {                                                              \
        auto bench_r_ = bench::run(name, iters, 11, [&]() { body; });\
        bench::print_result(bench_r_);                                \
    } while (0)

// BENCH with regression threshold (1.5× measured baseline).
// max_ns is the maximum allowed median — abort if exceeded.
// Also checks p90/p10 variance — aborts on noisy system.
#define BENCH_CHECK(name, iters, max_ns, body)                        \
    do {                                                              \
        auto bench_r_ = bench::run(name, iters, 11, [&]() { body; });\
        bench::print_result(bench_r_);                                \
        bench::check_regression(bench_r_, max_ns);                    \
    } while (0)

#define BENCH_ROUNDS(name, iters, rounds, body)                            \
    do {                                                                   \
        auto bench_r_ = bench::run(name, iters, rounds, [&]() { body; }); \
        bench::print_result(bench_r_);                                     \
    } while (0)

// BENCH_CHECK with explicit variance limit for structurally-variable
// benchmarks (burst fills, pipeline warmup, cache-state transitions).
#define BENCH_CHECK_V(name, iters, max_ns, max_var, body)              \
    do {                                                               \
        auto bench_r_ = bench::run(name, iters, 11, [&]() { body; }); \
        bench::print_result(bench_r_);                                 \
        bench::check_regression(bench_r_, max_ns, max_var);            \
    } while (0)

#define BENCH_ROUNDS_CHECK(name, iters, rounds, max_ns, body)              \
    do {                                                                   \
        auto bench_r_ = bench::run(name, iters, rounds, [&]() { body; }); \
        bench::print_result(bench_r_);                                     \
        bench::check_regression(bench_r_, max_ns);                         \
    } while (0)

// BENCH_ROUNDS_CHECK with explicit variance limit.
#define BENCH_ROUNDS_CHECK_V(name, iters, rounds, max_ns, max_var, body)   \
    do {                                                                   \
        auto bench_r_ = bench::run(name, iters, rounds, [&]() { body; }); \
        bench::print_result(bench_r_);                                     \
        bench::check_regression(bench_r_, max_ns, max_var);                \
    } while (0)

} // namespace bench
