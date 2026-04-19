#pragma once

// Benchmark harness.
//
// Methodology — each primitive has a citation in its doc comment:
//   • RDTSC bracketing via LFENCE / RDTSCP + LFENCE — Intel, "How to
//     Benchmark Code Execution Times on Intel IA-32 and IA-64" (2010).
//   • Per-sample collection + sorted-percentile extraction with linear
//     interpolation — Hyndman & Fan (1996) type 7, the R default.
//   • Bootstrap confidence intervals — Efron (1979).
//   • Mann-Whitney U rank-sum for A/B comparison — Mann & Whitney (1947).
//   • HW cycle counters via perf_event_open + PERF_COUNT_HW_CPU_CYCLES /
//     PERF_COUNT_HW_REF_CPU_CYCLES — Linux perf subsystem.
//
// What the harness gives you:
//   bench::Run("name").samples(N).warmup(W).core(C).measure(body) → Report
//   Report → text + JSON; bootstrap CI on any percentile on demand.
//   bench::compare(a, b) → Mann-Whitney U; distinguishable at p<0.01 or not.
//
// By default each Run pins to the current core (or to an isolcpu CPU if
// one exists), reads cpufreq at the start and end of the run, and opens
// perf_event_open hardware counters for CPU cycles + reference cycles.
// Result: frequency-invariant cycles/op alongside wall-time ns/op, so a
// run captured on a turbo CPU and a run on a throttled CPU are directly
// comparable in cycles terms.
//
// If perf_event_open fails (paranoid level >2, missing /proc/sys support,
// containers without CAP_PERFMON), the harness degrades gracefully: ns
// numbers are unchanged, cycle fields read 0, and the text output omits
// the cycles column.
//
// Auto-batching averages over 2^k calls until the timed region exceeds
// 1000 cycles — batched percentiles are over batch means, labeled
// "[batch-avg]" in the output.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
  #include <x86intrin.h>
#endif

#ifdef __linux__
  #include <sched.h>
  #include <unistd.h>
  #include <sys/resource.h>
  #if __has_include(<linux/perf_event.h>)
    #include <linux/perf_event.h>
    #include <sys/syscall.h>
    #define BENCH_HAVE_PERF 1
  #else
    #define BENCH_HAVE_PERF 0
  #endif
#else
  #define BENCH_HAVE_PERF 0
#endif

namespace bench {

// ── Fenced RDTSC primitives ────────────────────────────────────────
//
// Intel's "How to Benchmark..." (2010) §3.2.1: LFENCE before RDTSC to
// serialize preceding loads/stores; RDTSCP + LFENCE at the end so that
// subsequent loads cannot be sampled into the timed region. Bare RDTSC
// is out-of-order-able with both adjacent instructions and is incorrect
// for short-region timing. Overhead ~15-30 cycles; Timer measures the
// exact value and we subtract it.

[[nodiscard, gnu::always_inline]] inline uint64_t rdtsc_start() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _mm_lfence();
    return __rdtsc();
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

[[nodiscard, gnu::always_inline]] inline uint64_t rdtsc_end() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int aux;
    const uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& v) noexcept {
    asm volatile("" : : "r,m"(v) : "memory");
}

template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T& v) noexcept {
    // "+m,r" prefers memory over register — avoids "impossible constraint"
    // for types GCC can't place in a register with -O3 inlining.
    asm volatile("" : "+m,r"(v) : : "memory");
}

[[gnu::always_inline]] inline void clobber() noexcept {
    asm volatile("" : : : "memory");
}

// ── Timer: ns/cycle calibration + rdtsc overhead (Meyer singleton) ──

class Timer {
 public:
    [[nodiscard, gnu::pure]] static double   ns_per_cycle()    noexcept { return instance().ns_per_cycle_; }
    [[nodiscard, gnu::pure]] static uint64_t overhead_cycles() noexcept { return instance().overhead_cycles_; }
    [[nodiscard, gnu::pure]] static double   overhead_ns()     noexcept { return static_cast<double>(overhead_cycles()) * ns_per_cycle(); }
    [[nodiscard, gnu::pure]] static double   tsc_freq_hz()     noexcept { return (ns_per_cycle() > 0) ? 1e9 / ns_per_cycle() : 0.0; }

    [[nodiscard, gnu::pure]] static double to_ns(uint64_t cycles) noexcept {
        return static_cast<double>(cycles) * ns_per_cycle();
    }

 private:
    Timer() noexcept
        : ns_per_cycle_{calibrate_()},
          overhead_cycles_{measure_overhead_()} {}

    [[nodiscard]] static Timer& instance() noexcept {
        static Timer t;
        return t;
    }

    // Correlate TSC vs steady_clock over 200 ms. Longer window dampens
    // transient frequency wobble at process start.
    [[nodiscard]] static double calibrate_() noexcept {
        using namespace std::chrono;
        constexpr auto window = milliseconds(200);

        (void)steady_clock::now();
        (void)rdtsc_start();

        const auto wall0 = steady_clock::now();
        const uint64_t tsc0 = rdtsc_start();
        const auto deadline = wall0 + window;
        while (steady_clock::now() < deadline) { /* spin */ }
        const uint64_t tsc1 = rdtsc_end();
        const auto wall1 = steady_clock::now();

        const double ns = static_cast<double>(
            duration_cast<nanoseconds>(wall1 - wall0).count());
        const double cycles = static_cast<double>(tsc1 - tsc0);
        return (cycles > 0) ? (ns / cycles) : 0.0;
    }

    [[nodiscard]] static uint64_t measure_overhead_() noexcept {
        uint64_t best = UINT64_MAX;
        for (int i = 0; i < 10'000; ++i) {
            const uint64_t t0 = rdtsc_start();
            const uint64_t t1 = rdtsc_end();
            const uint64_t d = t1 - t0;
            if (d < best) best = d;
        }
        return best;
    }

    double   ns_per_cycle_;
    uint64_t overhead_cycles_;
};

// ── Platform introspection helpers ────────────────────────────────

namespace detail {

// Return the first CPU listed in /sys/.../cpu/isolated, or -1 if none.
// Format: "2-3,5" etc. We just want the first number.
[[nodiscard]] inline int first_isolated_cpu() noexcept {
#ifdef __linux__
    FILE* f = std::fopen("/sys/devices/system/cpu/isolated", "r");
    if (!f) return -1;
    char buf[256]{};
    int cpu = -1;
    if (std::fgets(buf, sizeof(buf), f)) {
        for (const char* p = buf; *p; ++p) {
            if (*p >= '0' && *p <= '9') {
                cpu = static_cast<int>(std::strtol(p, nullptr, 10));
                break;
            }
        }
    }
    std::fclose(f);
    return cpu;
#else
    return -1;
#endif
}

// Read /sys/.../cpu<N>/cpufreq/scaling_cur_freq (kHz) → Hz, 0 on failure.
[[nodiscard]] inline uint64_t read_cpu_freq_hz(int cpu) noexcept {
#ifdef __linux__
    if (cpu < 0) return 0;
    char path[128];
    std::snprintf(path, sizeof(path),
        "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    FILE* f = std::fopen(path, "r");
    if (!f) return 0;
    long khz = 0;
    const int got = std::fscanf(f, "%ld", &khz);
    std::fclose(f);
    return (got == 1 && khz > 0) ? static_cast<uint64_t>(khz) * 1000u : 0u;
#else
    (void)cpu;
    return 0;
#endif
}

} // namespace detail

// ── Perf counter pair: actual cycles + reference cycles ────────────
//
// PERF_COUNT_HW_CPU_CYCLES tracks unhalted core cycles — real work the
// core did, scales with turbo and down-clocking. PERF_COUNT_HW_REF_CPU_
// CYCLES ticks at the invariant reference rate — same as TSC on modern
// Intel/AMD. Their ratio is the instantaneous frequency multiplier.
//
// Opens with pid=0, cpu=-1 (this thread, any core) so pinning is
// respected. Unprivileged on /proc/sys/kernel/perf_event_paranoid ≤ 2;
// gracefully unavailable otherwise.
//
// Reads via ::read() — one syscall per counter, ~300 ns each. Called
// twice per run (start + end), so overhead is ~1.2 µs amortized over
// 100 k samples — negligible. For per-sample reads, rdpmc via mmap'd
// perf_event_mmap_page would be required; out of scope here.

class PerfCounters {
 public:
    struct Snapshot {
        uint64_t actual_cycles = 0;
        uint64_t ref_cycles    = 0;
    };

    [[nodiscard]] static std::optional<PerfCounters> open() noexcept {
#if BENCH_HAVE_PERF
        const int a = open_(PERF_COUNT_HW_CPU_CYCLES);
        const int r = open_(PERF_COUNT_HW_REF_CPU_CYCLES);
        if (a < 0 || r < 0) {
            if (a >= 0) ::close(a);
            if (r >= 0) ::close(r);
            return std::nullopt;
        }
        PerfCounters pc;
        pc.actual_fd_ = a;
        pc.ref_fd_    = r;
        return pc;
#else
        return std::nullopt;
#endif
    }

    [[nodiscard]] Snapshot read() const noexcept {
        Snapshot s{};
#if BENCH_HAVE_PERF
        if (actual_fd_ >= 0)
            (void)!::read(actual_fd_, &s.actual_cycles, sizeof(s.actual_cycles));
        if (ref_fd_ >= 0)
            (void)!::read(ref_fd_, &s.ref_cycles, sizeof(s.ref_cycles));
#endif
        return s;
    }

    PerfCounters() = default;
    PerfCounters(const PerfCounters&)            = delete;
    PerfCounters& operator=(const PerfCounters&) = delete;

    PerfCounters(PerfCounters&& o) noexcept
        : actual_fd_{o.actual_fd_}, ref_fd_{o.ref_fd_} {
        o.actual_fd_ = -1;
        o.ref_fd_    = -1;
    }
    PerfCounters& operator=(PerfCounters&& o) noexcept {
        if (this != &o) {
            close_();
            actual_fd_ = o.actual_fd_;
            ref_fd_    = o.ref_fd_;
            o.actual_fd_ = -1;
            o.ref_fd_    = -1;
        }
        return *this;
    }

    ~PerfCounters() { close_(); }

 private:
    void close_() noexcept {
#if BENCH_HAVE_PERF
        if (actual_fd_ >= 0) ::close(actual_fd_);
        if (ref_fd_    >= 0) ::close(ref_fd_);
#endif
        actual_fd_ = -1;
        ref_fd_    = -1;
    }

#if BENCH_HAVE_PERF
    [[nodiscard]] static int open_(uint64_t config) noexcept {
        struct perf_event_attr pe{};
        pe.type           = PERF_TYPE_HARDWARE;
        pe.size           = sizeof(pe);
        pe.config         = config;
        pe.disabled       = 0;
        pe.exclude_kernel = 1;   // user-space only
        pe.exclude_hv     = 1;
        return static_cast<int>(::syscall(SYS_perf_event_open,
            &pe, /*pid=*/0, /*cpu=*/-1, /*group_fd=*/-1, /*flags=*/0ul));
    }
#endif

    int actual_fd_ = -1;
    int ref_fd_    = -1;
};

// ── Percentile with linear interpolation (R type 7) ───────────────
//
// Hyndman & Fan (1996) enumerate nine sample-quantile definitions.
// Type 7 (R default) interpolates between the two samples straddling
// frac * (n-1). Removes the integer-floor bias that otherwise appears
// in commit-to-commit diffs.

[[nodiscard]] inline double percentile_interp(const std::vector<double>& sorted, double frac) noexcept {
    const size_t n = sorted.size();
    if (n == 0) return 0.0;
    if (n == 1) return sorted.front();
    const double q   = frac * static_cast<double>(n - 1);
    const size_t lo  = static_cast<size_t>(q);
    const size_t hi  = (lo + 1 < n) ? (lo + 1) : lo;
    const double f   = q - static_cast<double>(lo);
    return sorted[lo] * (1.0 - f) + sorted[hi] * f;
}

struct Percentiles {
    double p50 = 0, p75 = 0, p90 = 0, p95 = 0;
    double p99 = 0, p99_9 = 0, p99_99 = 0;
    double min = 0, max = 0, mean = 0, stddev = 0, cv = 0;
    size_t n = 0;

    [[nodiscard]] static Percentiles compute(std::vector<double>& ns_samples) noexcept {
        Percentiles p{};
        p.n = ns_samples.size();
        if (p.n == 0) return p;

        std::sort(ns_samples.begin(), ns_samples.end());
        p.min    = ns_samples.front();
        p.max    = ns_samples.back();
        p.p50    = percentile_interp(ns_samples, 0.50);
        p.p75    = percentile_interp(ns_samples, 0.75);
        p.p90    = percentile_interp(ns_samples, 0.90);
        p.p95    = percentile_interp(ns_samples, 0.95);
        p.p99    = percentile_interp(ns_samples, 0.99);
        p.p99_9  = percentile_interp(ns_samples, 0.999);
        p.p99_99 = percentile_interp(ns_samples, 0.9999);

        long double sum = 0, sum2 = 0;
        for (double v : ns_samples) { sum += v; sum2 += static_cast<long double>(v) * v; }
        p.mean = static_cast<double>(sum / p.n);
        if (p.n > 1) {
            const long double var = (sum2 - sum * sum / p.n) / (p.n - 1);
            p.stddev = static_cast<double>(std::sqrt(std::max<long double>(0, var)));
        }
        p.cv = (p.mean > 0) ? (p.stddev / p.mean) : 0.0;
        return p;
    }
};

// ── Bootstrap CI (Efron 1979) ─────────────────────────────────────

struct CI {
    double lo = 0;
    double hi = 0;
};

[[nodiscard]] inline CI bootstrap_ci(
    const std::vector<double>& ns_samples,
    double frac,
    size_t B = 1000,
    double alpha = 0.05,
    uint64_t seed = 0xBEEFCAFEDEADF00Dull)
{
    const size_t n = ns_samples.size();
    if (n < 30 || B == 0) return CI{};

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<size_t> pick(0, n - 1);

    std::vector<double> buf(n);
    std::vector<double> estimates;
    estimates.reserve(B);
    for (size_t b = 0; b < B; ++b) {
        for (size_t i = 0; i < n; ++i) buf[i] = ns_samples[pick(rng)];
        std::sort(buf.begin(), buf.end());
        estimates.push_back(percentile_interp(buf, frac));
    }
    std::sort(estimates.begin(), estimates.end());
    return {
        percentile_interp(estimates, alpha / 2.0),
        percentile_interp(estimates, 1.0 - alpha / 2.0),
    };
}

// ── Report ────────────────────────────────────────────────────────

struct Report {
    std::string         name;
    size_t              batch       = 1;     // > 1 → pct over batch means
    int                 pinned_cpu  = -1;
    double              wall_ns     = 0;     // total measurement wall time
    double              drift_pct   = 0;     // |first-half - second-half p50| / p50
    bool                drift_flag  = false; // drift_pct > 10 %
    Percentiles         pct{};

    // Frequency / cycle metadata — zero if unavailable.
    uint64_t            freq_start_hz       = 0;   // sysfs scaling_cur_freq at run start
    uint64_t            freq_end_hz         = 0;   // sysfs scaling_cur_freq at run end
    bool                freq_drift_flag     = false;
    double              measured_freq_hz    = 0;   // APERF/MPERF-style avg (perf counters)
    double              actual_cycles_per_op = 0;  // unhalted cycles / (samples × batch)
    double              ref_cycles_per_op    = 0;  // reference cycles / (samples × batch)

    std::vector<double> samples;  // kept for bootstrap_ci, compare

    [[nodiscard]] CI ci(double frac, size_t B = 1000) const {
        return bootstrap_ci(samples, frac, B);
    }

    [[nodiscard]] bool noisy(double cv_threshold = 0.05) const noexcept {
        return pct.cv > cv_threshold;
    }

    void print_text(FILE* out = stdout) const {
        std::fprintf(out,
            "  %-38s  p50=%7.2f  p90=%7.2f  p99=%7.2f  p99.9=%7.2f  "
            "max=%9.2f  μ=%7.2f  σ=%6.2f  cv=%4.1f%%",
            name.c_str(),
            pct.p50, pct.p90, pct.p99, pct.p99_9, pct.max,
            pct.mean, pct.stddev, pct.cv * 100.0);

        if (actual_cycles_per_op > 0) {
            std::fprintf(out, "  cyc=%6.1f", actual_cycles_per_op);
        }
        if (measured_freq_hz > 0) {
            std::fprintf(out, "  @%.2fGHz", measured_freq_hz / 1e9);
        }
        std::fprintf(out, "  n=%zu", pct.n);
        if (pinned_cpu >= 0)   std::fprintf(out, "  cpu%d",   pinned_cpu);
        if (batch > 1)         std::fprintf(out, "  [batch-avg]");
        if (noisy())           std::fprintf(out, "  [noisy]");
        if (drift_flag)        std::fprintf(out, "  [drift]");
        if (freq_drift_flag)   std::fprintf(out, "  [freq-drift]");
        std::fprintf(out, "\n");
    }

    void print_json(FILE* out) const {
        std::fprintf(out,
            "{\"name\":\"%s\",\"batch\":%zu,\"n\":%zu,\"cpu\":%d,"
            "\"p50\":%.3f,\"p75\":%.3f,\"p90\":%.3f,\"p95\":%.3f,"
            "\"p99\":%.3f,\"p99_9\":%.3f,\"p99_99\":%.3f,"
            "\"min\":%.3f,\"max\":%.3f,\"mean\":%.3f,\"stddev\":%.3f,"
            "\"cv\":%.5f,\"wall_ns\":%.0f,\"drift_pct\":%.3f,"
            "\"cycles_per_op\":%.3f,\"ref_cycles_per_op\":%.3f,"
            "\"freq_hz\":%.0f,\"freq_start_hz\":%lu,\"freq_end_hz\":%lu}",
            name.c_str(), batch, pct.n, pinned_cpu,
            pct.p50, pct.p75, pct.p90, pct.p95,
            pct.p99, pct.p99_9, pct.p99_99,
            pct.min, pct.max, pct.mean, pct.stddev, pct.cv,
            wall_ns, drift_pct,
            actual_cycles_per_op, ref_cycles_per_op,
            measured_freq_hz,
            static_cast<unsigned long>(freq_start_hz),
            static_cast<unsigned long>(freq_end_hz));
    }
};

// ── Mann-Whitney U (Mann & Whitney 1947) for A/B ──────────────────

struct Compare {
    std::string  a_name, b_name;
    double       delta_p50_pct   = 0;
    double       delta_p99_pct   = 0;
    double       delta_mean_pct  = 0;
    double       u               = 0;
    double       z               = 0;
    bool         distinguishable = false;    // |z| > 2.576 → p < 0.01

    void print_text(FILE* out = stdout) const {
        const char* flag = "  [indistinguishable]";
        if (distinguishable && delta_p99_pct >  5.0) flag = "  [REGRESS]";
        if (distinguishable && delta_p99_pct < -5.0) flag = "  [IMPROVE]";
        std::fprintf(out,
            "  Δ %s → %s:  Δp50=%+6.2f%%  Δp99=%+6.2f%%  Δμ=%+6.2f%%  z=%+5.2f%s\n",
            a_name.c_str(), b_name.c_str(),
            delta_p50_pct, delta_p99_pct, delta_mean_pct, z, flag);
    }
};

[[nodiscard]] inline Compare compare(const Report& a, const Report& b) {
    Compare c{.a_name = a.name, .b_name = b.name};

    const auto pct = [](double x, double y) {
        return (x > 0) ? 100.0 * (y - x) / x : 0.0;
    };
    c.delta_p50_pct  = pct(a.pct.p50,  b.pct.p50);
    c.delta_p99_pct  = pct(a.pct.p99,  b.pct.p99);
    c.delta_mean_pct = pct(a.pct.mean, b.pct.mean);

    const size_t n1 = a.samples.size();
    const size_t n2 = b.samples.size();
    if (n1 < 30 || n2 < 30) { c.distinguishable = false; return c; }

    struct Tagged { double v; uint8_t src; };
    std::vector<Tagged> all;
    all.reserve(n1 + n2);
    for (double v : a.samples) all.push_back({v, 0});
    for (double v : b.samples) all.push_back({v, 1});
    std::sort(all.begin(), all.end(),
              [](Tagged const& x, Tagged const& y) { return x.v < y.v; });

    long double r1 = 0;
    size_t i = 0;
    while (i < all.size()) {
        size_t j = i;
        while (j + 1 < all.size() && all[j + 1].v == all[i].v) ++j;
        const double avg_rank = (static_cast<double>(i + j) + 2.0) / 2.0;
        for (size_t k = i; k <= j; ++k) {
            if (all[k].src == 0) r1 += avg_rank;
        }
        i = j + 1;
    }

    const double u1    = static_cast<double>(r1) - static_cast<double>(n1) * (n1 + 1) / 2.0;
    const double u2    = static_cast<double>(n1) * n2 - u1;
    const double u_min = std::min(u1, u2);
    const double mu    = static_cast<double>(n1) * n2 / 2.0;
    const double sigma = std::sqrt(static_cast<double>(n1) * n2 * (n1 + n2 + 1) / 12.0);
    c.u = u_min;
    c.z = (sigma > 0) ? ((u_min - mu) / sigma) : 0.0;
    c.distinguishable = std::abs(c.z) > 2.576;
    return c;
}

// ── Run: fluent builder + measurement loop ─────────────────────────

class Run {
 public:
    explicit Run(std::string name) : name_{std::move(name)} {}

    // Pinning policy:
    //   default:    pin to isolcpu if available, else sched_getcpu()
    //   .core(N):   pin to CPU N
    //   .no_pin():  do not touch affinity
    Run& samples(size_t n) { samples_   = n; return *this; }
    Run& warmup(size_t n)  { warmup_    = n; return *this; }
    Run& batch(size_t n)   { batch_     = n; return *this; }
    Run& core(int c)       { core_      = c; pin_mode_ = Pin::Explicit; return *this; }
    Run& no_pin()          { pin_mode_  = Pin::None;   return *this; }

    template <typename Body>
    [[nodiscard]] Report measure(Body&& body) const {
        const int pinned_cpu = pin_();

        const double   nspc = Timer::ns_per_cycle();
        const uint64_t ovh  = Timer::overhead_cycles();
        const size_t   S    = samples_ ? samples_ : env_samples_();

        const size_t batch = (batch_ == 0) ? auto_batch_(body) : batch_;

        for (size_t i = 0; i < warmup_; ++i) body();

        auto perf = PerfCounters::open();
        PerfCounters::Snapshot perf_start{}, perf_end{};
        const uint64_t freq_start = detail::read_cpu_freq_hz(pinned_cpu);

        std::vector<double> ns_samples;
        ns_samples.reserve(S);

        if (perf) perf_start = perf->read();
        const auto wall0 = std::chrono::steady_clock::now();

        for (size_t i = 0; i < S; ++i) {
            const uint64_t t0 = rdtsc_start();
            for (size_t j = 0; j < batch; ++j) body();
            const uint64_t t1 = rdtsc_end();
            const uint64_t d  = (t1 > t0 + ovh) ? (t1 - t0 - ovh) : 0;
            ns_samples.push_back(
                (static_cast<double>(d) * nspc) / static_cast<double>(batch));
        }

        const auto wall1 = std::chrono::steady_clock::now();
        if (perf) perf_end = perf->read();
        const uint64_t freq_end = detail::read_cpu_freq_hz(pinned_cpu);

        // Drift: compare first-half vs second-half p50 as a proxy for
        // frequency or cache-state transitions during the run.
        double drift = 0;
        bool drift_flag = false;
        if (S >= 200) {
            const auto half = static_cast<std::ptrdiff_t>(S / 2);
            std::vector<double> h1(ns_samples.begin(), ns_samples.begin() + half);
            std::vector<double> h2(ns_samples.begin() + half, ns_samples.end());
            std::sort(h1.begin(), h1.end());
            std::sort(h2.begin(), h2.end());
            const double m1 = percentile_interp(h1, 0.5);
            const double m2 = percentile_interp(h2, 0.5);
            const double m  = (m1 + m2) / 2.0;
            drift = (m > 0) ? std::abs(m1 - m2) / m * 100.0 : 0.0;
            drift_flag = drift > 10.0;
        }

        Report r;
        r.name          = name_;
        r.batch         = batch;
        r.pinned_cpu    = pinned_cpu;
        r.drift_pct     = drift;
        r.drift_flag    = drift_flag;
        r.wall_ns       = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(wall1 - wall0).count());
        r.pct           = Percentiles::compute(ns_samples);
        r.samples       = std::move(ns_samples);

        // Sysfs-based freq drift check.
        r.freq_start_hz = freq_start;
        r.freq_end_hz   = freq_end;
        if (freq_start > 0 && freq_end > 0) {
            const auto hi = std::max(freq_start, freq_end);
            const auto lo = std::min(freq_start, freq_end);
            const double ratio = static_cast<double>(hi - lo) / static_cast<double>(hi);
            r.freq_drift_flag = ratio > 0.05;
        }

        // Perf counter-derived cycles/op and measured frequency.
        if (perf) {
            const uint64_t total_actual = perf_end.actual_cycles - perf_start.actual_cycles;
            const uint64_t total_ref    = perf_end.ref_cycles    - perf_start.ref_cycles;
            const uint64_t total_ops    = static_cast<uint64_t>(S) * batch;
            if (total_ops > 0) {
                r.actual_cycles_per_op = static_cast<double>(total_actual) / static_cast<double>(total_ops);
                r.ref_cycles_per_op    = static_cast<double>(total_ref)    / static_cast<double>(total_ops);
            }
            // ref_cycles tick at the TSC invariant rate; actual/ref gives
            // the frequency multiplier relative to TSC.
            if (total_ref > 0 && Timer::tsc_freq_hz() > 0) {
                const double mult = static_cast<double>(total_actual) / static_cast<double>(total_ref);
                r.measured_freq_hz = mult * Timer::tsc_freq_hz();
            }
        }

        return r;
    }

 private:
    enum class Pin : uint8_t { Auto, Explicit, None };

    std::string name_;
    size_t      samples_  = 0;
    size_t      warmup_   = 10'000;
    size_t      batch_    = 0;
    int         core_     = -1;
    Pin         pin_mode_ = Pin::Auto;

    [[nodiscard]] static size_t env_samples_() noexcept {
        if (const char* s = std::getenv("CRUCIBLE_BENCH_SAMPLES")) {
            const long n = std::strtol(s, nullptr, 10);
            if (n > 0) return static_cast<size_t>(n);
        }
        return 100'000;
    }

    // Returns the CPU actually pinned to, or -1 on None / failure.
    [[nodiscard]] int pin_() const noexcept {
#ifdef __linux__
        if (pin_mode_ == Pin::None) return sched_getcpu();

        int target = -1;
        if (pin_mode_ == Pin::Explicit) {
            target = core_;
        } else {
            target = detail::first_isolated_cpu();
            if (target < 0) target = sched_getcpu();
        }
        if (target < 0) return -1;

        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(target, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0) return sched_getcpu();
        return target;
#else
        return -1;
#endif
    }

    // Ramp batch size by 2× until one batch exceeds 1000 cycles — keeps
    // rdtsc overhead below ~3 % of the measured region. Cap at 2^18.
    template <typename Body>
    [[nodiscard]] size_t auto_batch_(Body&& body) const {
        constexpr uint64_t MIN_CYCLES     = 1000;
        constexpr size_t   MAX_BATCH      = 1u << 18;
        constexpr size_t   PILOT_SAMPLES  = 100;

        size_t batch = 1;
        while (batch <= MAX_BATCH) {
            uint64_t best = UINT64_MAX;
            for (size_t i = 0; i < PILOT_SAMPLES; ++i) {
                const uint64_t t0 = rdtsc_start();
                for (size_t j = 0; j < batch; ++j) body();
                const uint64_t t1 = rdtsc_end();
                const uint64_t d  = t1 - t0;
                if (d < best) best = d;
            }
            if (best >= MIN_CYCLES) return batch;
            batch *= 2;
        }
        return MAX_BATCH;
    }
};

// ── System info ────────────────────────────────────────────────────

inline void print_system_info(FILE* out = stdout) {
    std::fprintf(out, "=== system ===\n");

    FILE* f = std::fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[512];
        while (std::fgets(line, sizeof(line), f)) {
            if (std::strncmp(line, "model name", 10) == 0) {
                char* colon = std::strchr(line, ':');
                if (colon) {
                    char* v = colon + 1;
                    while (*v == ' ' || *v == '\t') ++v;
                    size_t len = std::strlen(v);
                    if (len && v[len - 1] == '\n') v[len - 1] = '\0';
                    std::fprintf(out, "  cpu:       %s\n", v);
                }
                break;
            }
        }
        std::fclose(f);
    }

    f = std::fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (f) {
        char g[64]{};
        if (std::fgets(g, sizeof(g), f)) {
            size_t len = std::strlen(g);
            if (len && g[len - 1] == '\n') g[len - 1] = '\0';
            std::fprintf(out, "  governor:  %s\n", g);
        }
        std::fclose(f);
    }

#ifdef __linux__
    const long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    std::fprintf(out, "  cpus:      %ld online\n", nproc);

    const int iso = detail::first_isolated_cpu();
    if (iso >= 0) std::fprintf(out, "  isolated:  cpu%d (from isolcpus)\n", iso);
#endif

    const double nspc = Timer::ns_per_cycle();
    const double ghz  = (nspc > 0) ? 1.0 / nspc : 0.0;
    std::fprintf(out, "  TSC:       %.4f ns/cycle (≈ %.3f GHz)\n", nspc, ghz);
    std::fprintf(out, "  RDTSC oh:  %lu cycles (≈ %.2f ns)\n",
                 static_cast<unsigned long>(Timer::overhead_cycles()),
                 Timer::overhead_ns());

    auto perf_probe = PerfCounters::open();
    std::fprintf(out, "  perf HW:   %s\n",
                 perf_probe ? "available (actual + ref cycles)" : "UNAVAILABLE (cycles/op = 0)");

    if (const char* s = std::getenv("CRUCIBLE_BENCH_SAMPLES")) {
        std::fprintf(out, "  samples:   %s (CRUCIBLE_BENCH_SAMPLES)\n", s);
    }
    std::fprintf(out, "\n");
}

inline void elevate_priority() noexcept {
#ifdef __linux__
    (void)setpriority(PRIO_PROCESS, 0, -10);
#endif
}

} // namespace bench
