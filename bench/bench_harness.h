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
//   • Kernel-side context via eBPF — 96 counters in a BPF_F_MMAPABLE
//     array map (bench/bpf/sense_hub.bpf.c, ported from symbiotic).
//     Tracepoint handlers run in kernel context on the event; the bench
//     side just does volatile mmap loads on entry and exit of the run,
//     so zero overhead lands inside the timed region.
//
// What the harness gives you:
//   bench::Run("name").samples(N).warmup(W).core(C).measure(body) → Report
//   Report → text + JSON; bootstrap CI on any percentile on demand.
//   bench::compare(a, b) → Mann-Whitney U; distinguishable at p<0.01 or not.
//
// Pin-by-default: uses /sys/devices/system/cpu/isolated first (so an
// isolcpu reserved via the kernel cmdline wins), falling back to
// sched_getcpu() only if no CPU is isolated. Each Run also reads
// cpufreq at the start and end of the run, and — when BPF is available
// — reads the 96-counter sense hub on both sides of the measurement.
// The reported deltas cover context switches, page faults, migrations,
// futex contention, I/O bytes, and the rest of the kernel surface the
// bench inadvertently touches.
//
// If BPF fails to load (missing CAP_BPF, kernel.unprivileged_bpf_disabled,
// kernel too old), the harness degrades gracefully: ns/cycle numbers are
// unchanged, sensory fields read zero, and the text output omits the
// sensory line.
//
// Auto-batching averages over 2^k calls until the timed region exceeds
// 1000 cycles — batched percentiles are over batch means, labeled
// "[batch-avg]" in the output.

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <span>
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
#endif

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
  // Promoted on 2026-05-03 (GAPS-004a) from bench-local bpf_senses.h
  // to the production observability substrate at
  // include/crucible/perf/SenseHub.h.  The namespace alias below
  // keeps every existing `bench::bpf::*` reference compiling at
  // zero runtime cost (alias is a compile-time symlink); a future
  // mechanical rename can drop the alias and replace
  // `bench::bpf::` with `crucible::perf::` everywhere in one sweep.
  #include <crucible/perf/SenseHub.h>
#endif

#include <crucible/rt/Hardening.h>
#include <crucible/rt/Policy.h>

namespace bench {

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
// Backwards-compatible alias: every legacy `bench::bpf::Snapshot`,
// `bench::bpf::SCHED_CTX_INVOL`, `bench::bpf::SenseHub::load()` etc.
// resolves to the canonical crucible::perf:: symbol post-promotion.
namespace bpf = ::crucible::perf;
#endif


// ── CpuId strong type (TypeSafe axiom) ─────────────────────────────
//
// Wraps a signed CPU index so that sched_getcpu()/sched_setaffinity
// arguments aren't interchangeable with raw `int`s flowing through the
// builder API. Mirrors CRUCIBLE_STRONG_ID in include/crucible/Types.h
// but uses a signed underlying type because POSIX CPU indices can be
// -1 ("unknown / no pin"). No arithmetic operators — callers must
// .raw() to compute, then rewrap.
//
//   • Default construct → CpuId::none() (raw() == -1, is_valid() == false)
//   • CpuId{sched_getcpu()} — explicit ctor, no implicit conversions
//   • is_valid() — true iff raw() >= 0 (a real CPU index)
//   • printed with %d after .raw() — matches existing format strings
struct CpuId {
    int raw_ = -1;

    constexpr CpuId() noexcept = default;
    constexpr explicit CpuId(int v) noexcept : raw_{v} {}

    [[nodiscard]] static constexpr CpuId none() noexcept { return CpuId{-1}; }

    [[nodiscard]] constexpr int  raw()      const noexcept { return raw_; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return raw_ >= 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_valid();
    }

    constexpr auto operator<=>(const CpuId&) const noexcept = default;
};
static_assert(sizeof(CpuId) == sizeof(int));

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

// Defeat dead-code elimination of values we want the optimizer to treat
// as consumed.
//
// We do NOT use the canonical Google-Benchmark / folly / nanobench form
//     asm volatile("" : "+m,r"(v) : : "memory")
//     asm volatile("" : :  "r,m"(v) : "memory")
// because both are subtly broken under modern GCC inlining + IPA:
//
//   * "+m,r"(v) is shorthand for "=m,r" output paired with "m,0" matching
//     input. When GCC selects the `r` alternative the matching `0` input
//     ties the input register to the output register (a temp); the asm
//     operates on the temp; nothing writes the temp back to v's storage.
//     Under inlining this leaves v's slot whatever it was at function
//     entry (uninitialized stack), and downstream lambda value-expr
//     substitutions that look up v by stack offset read the wrong bytes.
//     We hit this as a hard SIGSEGV in bench_pool_allocator's via-pointer
//     workload and minimized it to bugs/gcc-modref-miscompile/. Filed as
//     PR124958, closed INVALID by Andrew Pinski 2026-04-21: the asm is
//     wrong, not GCC.
//
//   * "r,m"(v) on a value: when GCC picks `r`, the asm sees a register
//     copy and the original storage is invisible. Earlier writes to v's
//     storage become DSE-eligible (PR109057, Jakub Jelinek).
//
//   * "+m,r" on objects > ~8 KB additionally forces a memcpy into a
//     temporary that is then never read back (PR105519).
//
// Pinski's recommended replacement, PR105519 c#3:
//     [[gnu::noipa]] void DoNotOptimize(T&) {}
// Empty function, no inline asm. `noipa` blocks inter-procedural
// analysis so the call cannot be deleted as dead, the argument is
// passed through real ABI registers / stack, and from the optimizer's
// point of view anything could happen inside. Cost is one real call
// instruction per use. Acceptable for benchmark scaffolding — the
// alternative is wrong-code at -O3.
//
// References:
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=124958  (our filing)
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109057  (r,m DSE)
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105519  (+m,r temp)

template <typename T>
[[gnu::noipa]] void do_not_optimize(T const& v) noexcept { (void)v; }

template <typename T>
[[gnu::noipa]] void do_not_optimize(T& v) noexcept { (void)v; }

[[gnu::always_inline]] inline void clobber() noexcept {
    asm volatile("" : : : "memory");
}

// ── clobber_array<T>(std::span<T>) — array-side DCE kill ──────────
//
// Companion to `do_not_optimize()` (single-value DCE kill) and
// `clobber()` (global memory barrier).  Use when the bench body
// fills or scans an array and the array's memory is otherwise dead
// at the timed region's exit — without this, the optimizer is free
// to elide the fill / scan because nothing observably consumes the
// result.
//
// Implementation: `[[gnu::noipa]]` empty function, same discipline
// as `do_not_optimize`.  The span (data pointer + length) is passed
// through real ABI registers; noipa blocks IPA so from the
// optimizer's point of view the function could read or write any
// memory reachable from those arguments — therefore the writes/
// reads to the array's elements MUST be materialized.
//
// Why not the asm `"+m,r"` form: per the project memory
// `feedback/project_gcc16_miscompile`, that idiom miscompiles on
// some types under GCC 16.  noipa is the recommended replacement.
//
// Two overloads — mutable and const — so callers don't need to
// const_cast a read-only scan.  std::span deduces T from any
// contiguous range (vector, array, C array, raw pointer + size).
//
// Cost: one real call instruction per use (same as do_not_optimize).
// Acceptable for benchmark scaffolding.
//
// Usage:
//
//   std::array<int, N> data;
//   for (auto& v : data) v = compute(...);
//   bench::clobber_array(std::span{data});  // forces materialization
//
//   std::vector<float> readings(N);
//   bench::Run("scan").measure([&]() noexcept {
//       float sum = 0;
//       for (float x : readings) sum += x;
//       bench::clobber_array(std::span<const float>{readings});
//       bench::do_not_optimize(sum);
//   });

template <typename T>
[[gnu::noipa]] void clobber_array(std::span<T> s) noexcept { (void)s; }

template <typename T>
[[gnu::noipa]] void clobber_array(std::span<const T> s) noexcept { (void)s; }

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

// ── BPF sense hub accessor (shared singleton) ──────────────────────
//
// All Runs in the same process share one loaded BPF program — the
// kernel tracepoints stay attached for the lifetime of the bench
// binary. First call loads + attaches; subsequent calls are a simple
// pointer return. Returns nullptr if BPF is unavailable (missing
// CAP_BPF, kernel.unprivileged_bpf_disabled=2, kernel too old).

namespace detail {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
[[nodiscard]] inline const bench::bpf::SenseHub* bpf_instance() noexcept {
    // SenseHub::load(effects::Init) takes a 1-byte init capability tag
    // (post-GAPS-004a hardening, 2026-05-03).  Static-init context is
    // load-bearing init; constructing Init{} here is the canonical
    // form.  Hot-path code holds no Init capability and therefore
    // cannot synthesize the argument — this guarantees the entire
    // call chain rooted at SenseHub::load() never reaches a hot frame.
    static std::optional<bench::bpf::SenseHub> slot =
        bench::bpf::SenseHub::load(::crucible::effects::Init{});
    return slot.has_value() ? &*slot : nullptr;
}
#else
[[nodiscard]] inline const void* bpf_instance() noexcept { return nullptr; }
#endif
} // namespace detail

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
    std::vector<double> estimates(B);
    for (size_t b = 0; b < B; ++b) {
        for (size_t i = 0; i < n; ++i) buf[i] = ns_samples[pick(rng)];
        std::sort(buf.begin(), buf.end());
        estimates[b] = percentile_interp(buf, frac);
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
    size_t              batch       = 1;     // > 1 → pct over batch means  // TODO: strong type
    CpuId               pinned_cpu{};        // CpuId::none() = -1 / no pin
    double              wall_ns     = 0;     // total measurement wall time
    double              drift_pct   = 0;     // |first-half - second-half p50| / p50
    bool                drift_flag  = false; // drift_pct > 10 %
    Percentiles         pct{};

    // sysfs-derived frequency bracketing (not PMU).
    uint64_t            freq_start_hz       = 0;   // scaling_cur_freq at run start
    uint64_t            freq_end_hz         = 0;   // scaling_cur_freq at run end
    bool                freq_drift_flag     = false;

    // Cycles/op computed from rdtsc-derived ns and the TSC frequency
    // (Timer::tsc_freq_hz). This is wall-time cycles, not PMU retired
    // cycles — but on a pinned, non-throttled core the two are equal
    // to within a few percent and suffice for cross-commit comparison.
    double              cycles_per_op        = 0;

    // Kernel-side context via eBPF sense hub. All zero when BPF is
    // unavailable. Deltas over the measured run (monotonic counters).
    //
    // For the gauge-valued Idx slots listed in bpf_senses.h's
    // Snapshot::operator- comment (FD_CURRENT, TCP_{MIN,MAX}_SRTT_US,
    // TCP_LAST_CWND, THERMAL_MAX_TRIP, SIGNAL_LAST_SIGNO, OOM_KILL_US,
    // RECLAIM_STALL_LOOPS, NET_TCP_*, NET_UDP_ACTIVE, NET_UNIX_ACTIVE,
    // RSS_{ANON,FILE,SHMEM}_BYTES, RSS_SWAP_ENTRIES) the post - pre
    // difference is meaningless; callers wanting those instantaneous
    // values should pull them from the post snapshot directly.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    bench::bpf::Snapshot bpf_delta{};
    size_t               bpf_attached = 0;  // # programs the kernel accepted
#endif

    std::vector<double> samples;  // kept for bootstrap_ci, compare

    // Report owns a potentially-large sample vector (~100k doubles by
    // default = 800 KB). Copying is almost always an accidental pessim-
    // ization — accepted callers either take `const Report&` (compare,
    // print_*) or move from a return value. Delete copy, keep move.
    Report() = default;
    ~Report() = default;
    Report(const Report&) = delete("copy of Report is almost always unintentional; use move semantics or pass by const&");
    Report& operator=(const Report&) = delete("copy-assignment of Report is almost always unintentional; use move or const&");
    Report(Report&&) noexcept = default;
    Report& operator=(Report&&) noexcept = default;

    // Bootstrap CI on a sample percentile. Returns CI{0,0} on insufficient
    // samples (n < 30) — callers that need to distinguish "no data" from
    // a genuine interval centred at zero should use `ci_opt()`, which
    // surfaces the "not enough samples" case as std::nullopt.
    //
    // Marked noexcept: bootstrap_ci allocates std::vectors, but OOM in
    // this project aborts() per the MemSafe axiom, so throwing here is
    // structurally impossible and std::terminate via the noexcept barrier
    // is the desired outcome if an allocator ever did throw.
    [[nodiscard]] CI ci(double frac, size_t B = 1000) const noexcept {
        return bootstrap_ci(samples, frac, B);
    }

    // Same as ci() but distinguishes "insufficient samples" (nullopt)
    // from a computed interval. Implicitly wraps the CI-returning
    // bootstrap_ci — a CI with lo == hi == 0 still reports has_value()
    // == true; callers that need the n < 30 signal should gate on
    // samples.size() >= 30 before consulting the return.
    [[nodiscard]] std::optional<CI> ci_opt(double frac, size_t B = 1000) const {
        if (samples.size() < 30) return std::nullopt;
        return bootstrap_ci(samples, frac, B);
    }

    [[nodiscard]] bool noisy(double cv_threshold = 0.05) const noexcept {
        return pct.cv > cv_threshold;
    }

    // Auto-scaled ns formatter for the main summary columns: renders a
    // fractional-ns double as "NN.NNns" / "NN.NNµs" / "NN.NNms" /
    // "NN.NNs" with 2 decimals, right-padded to 9 bytes for column
    // alignment. µs is 2-byte UTF-8 so alignment is slightly off on
    // µs/ms/s columns — 9 bytes fits up to "9999.99ns" and "999.99µs".
    static void fmt_ns_(char* buf, size_t len, double ns) noexcept {
        if      (ns >= 1e9) std::snprintf(buf, len, "%.2fs",  ns / 1e9);
        else if (ns >= 1e6) std::snprintf(buf, len, "%.2fms", ns / 1e6);
        else if (ns >= 1e3) std::snprintf(buf, len, "%.2fµs", ns / 1e3);
        else                std::snprintf(buf, len, "%.2fns", ns);
    }

    // Writes a single summary line (optionally followed by the BPF
    // sensory line) to `out`. Every underlying call is C stdio and
    // std::string::c_str() — no throwing paths, so noexcept.
    void print_text(FILE* out = stdout) const noexcept {
        char p50_b[32], p90_b[32], p99_b[32], p999_b[32], max_b[32], mean_b[32], sigma_b[32];
        fmt_ns_(p50_b,  sizeof(p50_b),  pct.p50);
        fmt_ns_(p90_b,  sizeof(p90_b),  pct.p90);
        fmt_ns_(p99_b,  sizeof(p99_b),  pct.p99);
        fmt_ns_(p999_b, sizeof(p999_b), pct.p99_9);
        fmt_ns_(max_b,  sizeof(max_b),  pct.max);
        fmt_ns_(mean_b, sizeof(mean_b), pct.mean);
        fmt_ns_(sigma_b, sizeof(sigma_b), pct.stddev);
        std::fprintf(out,
            "  %-38s  p50=%9s  p90=%9s  p99=%9s  p99.9=%9s  "
            "max=%10s  μ=%9s  σ=%9s  cv=%4.1f%%",
            name.c_str(),
            p50_b, p90_b, p99_b, p999_b, max_b, mean_b, sigma_b,
            pct.cv * 100.0);

        if (cycles_per_op > 0.0) {
            std::fprintf(out, "  cyc=%7.1f", cycles_per_op);
        }
        if (freq_start_hz > 0) {
            std::fprintf(out, "  @%.2fGHz", static_cast<double>(freq_start_hz) / 1e9);
        }
        std::fprintf(out, "  n=%zu", pct.n);
        if (pinned_cpu.is_valid()) std::fprintf(out, "  cpu%d", pinned_cpu.raw());
        if (batch > 1)         std::fprintf(out, "  [batch-avg]");
        if (noisy())           std::fprintf(out, "  [noisy]");
        if (drift_flag)        std::fprintf(out, "  [drift]");
        if (freq_drift_flag)   std::fprintf(out, "  [freq-drift]");
        std::fprintf(out, "\n");

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        // One-line sensory summary. When BPF is attached and every
        // interesting counter is zero we still emit "└─ sched clean"
        // as confirmation that senses ARE attached (so the reader can
        // trust the silence). When any counter fires, only the fields
        // that actually have a delta are printed.
        print_bpf_summary_(out);
#endif
    }

    // Minimal RFC-8259 string escape — covers every char that would
    // make a JSON parser unhappy inside a "..."-quoted value. Names in
    // benches can contain `"` (e.g. arena.copy_string(\"relu\")), and
    // are user-controlled by the Run constructor. Escape these before
    // emission to produce valid JSON unconditionally.
    static void fprint_json_string(FILE* out, std::string_view s) noexcept {
        std::fputc('"', out);
        for (unsigned char c : s) {
            switch (c) {
                case '"':  std::fputs("\\\"", out); break;
                case '\\': std::fputs("\\\\", out); break;
                case '\b': std::fputs("\\b",  out); break;
                case '\f': std::fputs("\\f",  out); break;
                case '\n': std::fputs("\\n",  out); break;
                case '\r': std::fputs("\\r",  out); break;
                case '\t': std::fputs("\\t",  out); break;
                default:
                    if (c < 0x20) {
                        std::fprintf(out, "\\u%04x", static_cast<unsigned>(c));
                    } else {
                        std::fputc(static_cast<char>(c), out);
                    }
            }
        }
        std::fputc('"', out);
    }

    // JSON emitter — same argument as print_text(): C stdio + noexcept
    // accessors only, no throwing paths.
    void print_json(FILE* out) const noexcept {
        std::fputc('{', out);
        std::fputs("\"name\":", out);
        fprint_json_string(out, name);
        std::fprintf(out,
            ",\"batch\":%zu,\"n\":%zu,\"cpu\":%d,"
            "\"p50\":%.3f,\"p75\":%.3f,\"p90\":%.3f,\"p95\":%.3f,"
            "\"p99\":%.3f,\"p99_9\":%.3f,\"p99_99\":%.3f,"
            "\"min\":%.3f,\"max\":%.3f,\"mean\":%.3f,\"stddev\":%.3f,"
            "\"cv\":%.5f,\"wall_ns\":%.0f,\"drift_pct\":%.3f,"
            "\"cycles_per_op\":%.3f,"
            "\"freq_start_hz\":%lu,\"freq_end_hz\":%lu",
            batch, pct.n, pinned_cpu.raw(),
            pct.p50, pct.p75, pct.p90, pct.p95,
            pct.p99, pct.p99_9, pct.p99_99,
            pct.min, pct.max, pct.mean, pct.stddev, pct.cv,
            wall_ns, drift_pct,
            cycles_per_op,
            static_cast<unsigned long>(freq_start_hz),
            static_cast<unsigned long>(freq_end_hz));
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        print_bpf_json_(out);
#endif
        std::fputc('}', out);
    }

 private:
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    // Print every monotonic counter that actually incremented during the
    // run, in unit-scaled form. The gauge-valued Idx slots listed in
    // bpf_senses.h's Snapshot::operator- comment (FD_CURRENT, TCP_
    // {MIN,MAX}_SRTT_US, TCP_LAST_CWND, THERMAL_MAX_TRIP, SIGNAL_LAST_
    // SIGNO, OOM_KILL_US, RECLAIM_STALL_LOOPS, NET_TCP_*, NET_UDP_ACTIVE,
    // NET_UNIX_ACTIVE, RSS_{ANON,FILE,SHMEM}_BYTES, RSS_SWAP_ENTRIES)
    // are deliberately excluded from kAll — their (post - pre) delta
    // saturates to zero in Snapshot::operator-. Callers wanting raw
    // gauge values should pull them from the post snapshot directly.
    //
    // Output:
    //   └─ clean                                   (no monotonic counter moved)
    //   └─ 6 preempt · 243 pgfault · 4.3ms softirq · 48 mmap
    //
    // "N label" reads as English. Times auto-scale ns → µs → ms → s,
    // bytes auto-scale to KB/MB/GB, counts to k/M/G above 10k.

    enum class Unit : uint8_t { Count, Ns, Bytes };

    struct Field {
        const char*       label;
        bench::bpf::Idx   idx;
        Unit              unit;
    };

    // Every counter whose (post - pre) is meaningful. Order = the story
    // the reader walks: first what interrupted us, then what the bench
    // touched (memory → sync → I/O → thread mgmt), finally reliability.
    static constexpr Field kAll[] = {
        // Scheduling interference.
        {"preempt",   bench::bpf::SCHED_CTX_INVOL,        Unit::Count},
        {"yield",     bench::bpf::SCHED_CTX_VOL,          Unit::Count},
        {"migrate",   bench::bpf::SCHED_MIGRATIONS,       Unit::Count},
        {"runtime",   bench::bpf::SCHED_RUNTIME_NS,       Unit::Ns},
        {"wait",      bench::bpf::SCHED_WAIT_NS,          Unit::Ns},
        {"sleep",     bench::bpf::SCHED_SLEEP_NS,         Unit::Ns},
        {"iowait",    bench::bpf::SCHED_IOWAIT_NS,        Unit::Ns},
        {"blocked",   bench::bpf::SCHED_BLOCKED_NS,       Unit::Ns},
        {"softirq",   bench::bpf::SOFTIRQ_STOLEN_NS,      Unit::Ns},
        {"wake_rx",   bench::bpf::WAKEUPS_RECEIVED,       Unit::Count},
        {"wake_tx",   bench::bpf::WAKEUPS_SENT,           Unit::Count},
        {"freq_chg",  bench::bpf::CPU_FREQ_CHANGES,       Unit::Count},
        {"tid_new",   bench::bpf::THREADS_CREATED,        Unit::Count},
        {"tid_end",   bench::bpf::THREADS_EXITED,         Unit::Count},

        // Memory.
        {"pgfault",   bench::bpf::MEM_PAGE_FAULTS_MIN,    Unit::Count},
        {"majfault",  bench::bpf::MEM_PAGE_FAULTS_MAJ,    Unit::Count},
        {"mmap",      bench::bpf::MEM_MMAP_COUNT,         Unit::Count},
        {"munmap",    bench::bpf::MEM_MUNMAP_COUNT,       Unit::Count},
        {"brk",       bench::bpf::MEM_BRK_CALLS,          Unit::Count},
        {"reclaim_n", bench::bpf::DIRECT_RECLAIM_COUNT,   Unit::Count},
        {"reclaim_t", bench::bpf::DIRECT_RECLAIM_NS,      Unit::Ns},
        {"swap_out",  bench::bpf::SWAP_OUT_PAGES,         Unit::Count},
        {"thp_ok",    bench::bpf::THP_COLLAPSE_OK,        Unit::Count},
        {"thp_fail",  bench::bpf::THP_COLLAPSE_FAIL,      Unit::Count},
        {"numa",      bench::bpf::NUMA_MIGRATE_PAGES,     Unit::Count},
        {"compact",   bench::bpf::COMPACTION_STALLS,      Unit::Count},
        {"extfrag",   bench::bpf::EXTFRAG_EVENTS,         Unit::Count},

        // Sync contention.
        {"futex",     bench::bpf::FUTEX_WAIT_COUNT,       Unit::Count},
        {"futex_t",   bench::bpf::FUTEX_WAIT_NS,          Unit::Ns},
        {"klock",     bench::bpf::KERNEL_LOCK_COUNT,      Unit::Count},
        {"klock_t",   bench::bpf::KERNEL_LOCK_NS,         Unit::Ns},

        // I/O (syscall-level).
        {"read",      bench::bpf::IO_READ_BYTES,          Unit::Bytes},
        {"write",     bench::bpf::IO_WRITE_BYTES,         Unit::Bytes},
        {"r_ops",     bench::bpf::IO_READ_OPS,            Unit::Count},
        {"w_ops",     bench::bpf::IO_WRITE_OPS,           Unit::Count},
        {"fd_open",   bench::bpf::FD_OPEN_OPS,            Unit::Count},

        // Block I/O (device-level).
        {"disk_r",    bench::bpf::DISK_READ_BYTES,        Unit::Bytes},
        {"disk_w",    bench::bpf::DISK_WRITE_BYTES,       Unit::Bytes},
        {"disk_t",    bench::bpf::DISK_IO_LATENCY_NS,     Unit::Ns},
        {"disk_n",    bench::bpf::DISK_IO_COUNT,          Unit::Count},
        {"pg_miss",   bench::bpf::PAGE_CACHE_MISSES,      Unit::Count},
        {"readahead", bench::bpf::READAHEAD_PAGES,        Unit::Count},
        {"unplug",    bench::bpf::IO_UNPLUG_COUNT,        Unit::Count},
        {"throttle",  bench::bpf::WRITE_THROTTLE_JIFFIES, Unit::Count},

        // Network.
        {"tx",        bench::bpf::NET_TX_BYTES,           Unit::Bytes},
        {"rx",        bench::bpf::NET_RX_BYTES,           Unit::Bytes},
        {"retrans",   bench::bpf::TCP_RETRANSMIT_COUNT,   Unit::Count},
        {"rst",       bench::bpf::TCP_RST_SENT,           Unit::Count},
        {"sk_err",    bench::bpf::TCP_ERROR_COUNT,        Unit::Count},
        {"skb_drop",  bench::bpf::SKB_DROP_COUNT,         Unit::Count},
        {"cng_loss",  bench::bpf::TCP_CONG_LOSS,          Unit::Count},

        // Reliability (delta-meaningful signals only).
        {"sig_fatal", bench::bpf::SIGNAL_FATAL_COUNT,     Unit::Count},
        {"oom_kills", bench::bpf::OOM_KILLS_SYSTEM,       Unit::Count},
        {"mce",       bench::bpf::MCE_COUNT,              Unit::Count},
    };

    static void print_scaled_(FILE* out, uint64_t v, Unit u) noexcept {
        switch (u) {
        case Unit::Count:
            if      (v >= 1'000'000'000) std::fprintf(out, "%.1fG", static_cast<double>(v) / 1e9);
            else if (v >= 1'000'000)     std::fprintf(out, "%.1fM", static_cast<double>(v) / 1e6);
            else if (v >= 10'000)        std::fprintf(out, "%.1fk", static_cast<double>(v) / 1e3);
            else                         std::fprintf(out, "%lu",   v);
            break;
        case Unit::Ns:
            if      (v >= 1'000'000'000) std::fprintf(out, "%.1fs",  static_cast<double>(v) / 1e9);
            else if (v >= 1'000'000)     std::fprintf(out, "%.1fms", static_cast<double>(v) / 1e6);
            else if (v >= 1'000)         std::fprintf(out, "%.1fµs", static_cast<double>(v) / 1e3);
            else                         std::fprintf(out, "%luns",  v);
            break;
        case Unit::Bytes:
            if      (v >= (1ULL << 30)) std::fprintf(out, "%.1fGB", static_cast<double>(v) / static_cast<double>(1ULL << 30));
            else if (v >= (1ULL << 20)) std::fprintf(out, "%.1fMB", static_cast<double>(v) / static_cast<double>(1ULL << 20));
            else if (v >= (1ULL << 10)) std::fprintf(out, "%.1fKB", static_cast<double>(v) / 1024.0);
            else                        std::fprintf(out, "%luB",   v);
            break;
        }
    }

    void print_bpf_summary_(FILE* out) const noexcept {
        if (bpf_attached == 0) return;
        const auto& d = bpf_delta;

        std::fprintf(out, "     └─ ");
        bool first = true;
        for (const Field& f : kAll) {
            const uint64_t v = d[f.idx];
            if (v == 0) continue;
            if (!first) std::fprintf(out, " · ");
            first = false;
            std::fprintf(out, "%s=", f.label);
            print_scaled_(out, v, f.unit);
        }
        if (first) std::fprintf(out, "clean");
        std::fputc('\n', out);
    }

    // JSON: emit every counter in the kAll table as a stable schema
    // (all present whether zero or not) so downstream consumers can
    // rely on field presence.
    void print_bpf_json_(FILE* out) const noexcept {
        if (bpf_attached == 0) return;
        const auto& d = bpf_delta;
        std::fprintf(out, ",\"bpf\":{\"attached\":%zu", bpf_attached);
        for (const Field& f : kAll) {
            std::fprintf(out, ",\"%s\":%lu", f.label, d[f.idx]);
        }
        std::fputc('}', out);
    }
#endif
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

    // One-line summary — C stdio + std::string::c_str() only, noexcept
    // for the same reasons as Report::print_text.
    void print_text(FILE* out = stdout) const noexcept {
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
    std::vector<Tagged> all(n1 + n2);
    for (size_t i = 0; i < n1; ++i) all[i]      = {a.samples[i], 0};
    for (size_t i = 0; i < n2; ++i) all[n1 + i] = {b.samples[i], 1};
    std::sort(all.begin(), all.end(),
              [](Tagged const& x, Tagged const& y) { return x.v < y.v; });

    // Rank the union of A and B. In a tie group spanning positions
    // [i..j] (inclusive), every element gets the average of the ranks
    // i+1 through j+1. Simultaneously accumulate the tie-correction
    // sum T = Σ(tᵢ³ − tᵢ), where tᵢ is each tie-group's size; used
    // below to adjust sigma for ties.
    long double r1 = 0;
    long double tie_sum = 0;
    size_t i = 0;
    while (i < all.size()) {
        size_t j = i;
        while (j + 1 < all.size() && all[j + 1].v == all[i].v) ++j;
        const double avg_rank = (static_cast<double>(i + j) + 2.0) / 2.0;
        const long double t = static_cast<long double>(j - i + 1);
        tie_sum += t * t * t - t;  // 0 for tie groups of size 1
        for (size_t k = i; k <= j; ++k) {
            if (all[k].src == 0) r1 += avg_rank;
        }
        i = j + 1;
    }

    const double    u1    = static_cast<double>(r1) - static_cast<double>(n1) * (n1 + 1) / 2.0;
    const double    u2    = static_cast<double>(n1) * n2 - u1;
    const double    u_min = std::min(u1, u2);
    const double    mu    = static_cast<double>(n1) * n2 / 2.0;

    // Tie-corrected variance (Mann & Whitney 1947 §5; Siegel 1956):
    //   sigma² = (n1·n2 / 12) · (N + 1 − T / (N·(N − 1)))
    // For T = 0 (no ties) this reduces to the classical form.
    const long double N       = static_cast<long double>(n1) + static_cast<long double>(n2);
    const long double n1n2_12 = (static_cast<long double>(n1) * n2) / 12.0L;
    const long double Ncorr   = (N > 1.0L)
        ? (N + 1.0L - tie_sum / (N * (N - 1.0L)))
        : (N + 1.0L);
    const double sigma = static_cast<double>(std::sqrt(std::max<long double>(0, n1n2_12 * Ncorr)));

    // Continuity correction: subtract 0.5 from |u_min − mu| before
    // dividing by sigma (normal approximation of a discrete statistic).
    // Sign of z preserves direction (is A tending below or above B?).
    const double diff  = u_min - mu;
    const double adj   = (diff >= 0.0 ? +1.0 : -1.0)
                       * std::max(0.0, std::abs(diff) - 0.5);

    c.u = u_min;
    c.z = (sigma > 0) ? (adj / sigma) : 0.0;
    c.distinguishable = std::abs(c.z) > 2.576;
    return c;
}

// ── Run: fluent builder + measurement loop ─────────────────────────

class Run {
 public:
    explicit Run(std::string name) : name_{std::move(name)} {}

    // Pinning policy:
    //   default:       pin to first isolcpu (else sched_getcpu())
    //   .core(N) N≥0:  pin to CPU N
    //   .core(-1):     treat as .default; still applies affinity to
    //                  isolcpu/sched_getcpu() (previously silently
    //                  returned without pinning, so env CRUCIBLE_BENCH_CORE
    //                  unset → no affinity ever applied)
    //   .no_pin():     do not touch affinity
    //
    // [[nodiscard]] on every setter — the fluent builder is worthless if
    // the returned Run& is thrown away before .measure().
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& samples(size_t n) noexcept { samples_ = n; return *this; }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& warmup(size_t n)  noexcept { warmup_  = n; return *this; }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& batch(size_t n)   noexcept { batch_   = n; return *this; }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& core(int c)       noexcept { core_    = CpuId{c}; pin_mode_ = Pin::Explicit; return *this; }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& no_pin()          noexcept { pin_mode_ = Pin::None; return *this; }

    // Apply a `crucible::rt::Policy` to the measuring thread for the
    // duration of the Run. When set, the policy's `hot_core` selector
    // drives pinning and our legacy pin_() path is skipped entirely —
    // so `.hardening(p)` unconditionally wins over any prior or
    // subsequent `.core(N)` / `.no_pin()` in the same builder chain.
    // The RAII guard returned by crucible::rt::apply() reverts sched
    // class, affinity, mlock'd regions, and THP flag when measure()
    // returns.
    //
    // Default: no hardening (have_hardening_ == false → legacy pin_()).
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& hardening(const crucible::rt::Policy& p) noexcept {
        hardening_ = p;
        have_hardening_ = true;
        return *this;
    }

    // Cap the Run's total wall time. `ms=0` disables the cap (fall
    // back to the raw samples_ × batch iteration count). Default
    // budget is 10 seconds per Run — enough for stable p99.9 on any
    // sensible body, short enough that heavy bodies (~1 ms) don't
    // iterate so many times that glibc's heap pool stops returning
    // pages to the OS (observed: bench_graph at N=4096 built 1 MB
    // per body, 100k samples → 20 GB RSS before the cap).
    //
    // Behavior when the cap hits:
    //   - Warmup stops as soon as wall exceeds max_wall_ms_/4.
    //   - The sample loop stops at the first sample boundary past
    //     max_wall_ms_. Collected samples are kept (ns_samples.size()
    //     < samples_), drift calculation and Percentiles::compute
    //     work on whatever landed.
    //
    // Override with env CRUCIBLE_BENCH_WALL_MS=<ms>. Explicit
    // `.max_wall_ms(0)` disables capping even with env set.
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& max_wall_ms(size_t ms) noexcept {
        max_wall_ms_ = ms;
        have_max_wall_ = true;
        return *this;
    }

    template <typename Body>
    [[nodiscard]] Report measure(Body&& body) const {
        // Resolve the policy: explicit .hardening() wins, else env var
        // CRUCIBLE_BENCH_HARDENING=production|cloud_vm|dev_quiet|none, else the
        // legacy pin_() path.
        crucible::rt::AppliedPolicy hardening_guard;
        CpuId pinned_cpu;
        if (have_hardening_) {
            hardening_guard = crucible::rt::apply(hardening_);
            pinned_cpu      = CpuId{hardening_guard.pinned_cpu()};
        } else if (auto env = env_hardening_(); env.has_value()) {
            hardening_guard = crucible::rt::apply(*env);
            pinned_cpu      = CpuId{hardening_guard.pinned_cpu()};
        } else {
            pinned_cpu = pin_();
        }

        const double   nspc = Timer::ns_per_cycle();
        const uint64_t ovh  = Timer::overhead_cycles();
        const size_t   S    = samples_ ? samples_ : env_samples_();

        const size_t batch = (batch_ == 0) ? auto_batch_(body) : batch_;

        // Wall-time cap: explicit `.max_wall_ms()` wins; else env var
        // CRUCIBLE_BENCH_WALL_MS; else default `kDefaultMaxWallMs`.
        // Zero disables capping entirely.
        const size_t wall_cap_ms = have_max_wall_
            ? max_wall_ms_
            : env_wall_ms_().value_or(kDefaultMaxWallMs);
        const auto start_all = std::chrono::steady_clock::now();
        const auto wall_budget = std::chrono::milliseconds(wall_cap_ms);
        const auto warmup_budget = wall_budget / 4;

        for (size_t i = 0; i < warmup_; ++i) {
            body();
            // Budget check every 64 iterations — steady_clock::now() is
            // ~20 ns, amortizing it keeps overhead under 1% for ≥2 µs
            // bodies and invisible for anything heavier.
            if (wall_cap_ms > 0 && (i & 63) == 63 &&
                std::chrono::steady_clock::now() - start_all > warmup_budget) {
                break;
            }
        }

        const uint64_t freq_start = detail::read_cpu_freq_hz(pinned_cpu.raw());

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        const bench::bpf::SenseHub* hub = detail::bpf_instance();
        bench::bpf::Snapshot        bpf_pre{};
        bench::bpf::Snapshot        bpf_post{};
#endif

        // Pre-sized to S; if the wall-cap cutoff fires early, we trim to
        // `filled` below so downstream stats (sort, percentile) only see
        // real samples — never default-zero trailing slots.
        std::vector<double> ns_samples(S);
        size_t              filled = 0;

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        if (hub != nullptr) bpf_pre = hub->read();
#endif
        const auto wall0 = std::chrono::steady_clock::now();

        for (size_t i = 0; i < S; ++i) {
            const uint64_t t0 = rdtsc_start();
            for (size_t j = 0; j < batch; ++j) body();
            const uint64_t t1 = rdtsc_end();
            // RDTSCP+LFENCE guarantees t1 >= t0 (each RDTSC pair is
            // monotone on a single core); modular unsigned subtraction
            // is therefore safe. The older form `(t1 > t0 + ovh)`
            // wraps to false when t0 + ovh overflows UINT64_MAX, which
            // can happen after a long runtime on a 4+ GHz core.
            const uint64_t raw = t1 - t0;
            const uint64_t d   = (raw > ovh) ? (raw - ovh) : 0;
            ns_samples[i] =
                (static_cast<double>(d) * nspc) / static_cast<double>(batch);
            ++filled;
            // Wall-budget cut-off. Checked every 64 samples to keep
            // clock overhead negligible on fast bodies; heavy bodies
            // hit the check often enough because each iteration is
            // already long relative to steady_clock::now() (~20 ns).
            if (wall_cap_ms > 0 && (i & 63) == 63 &&
                std::chrono::steady_clock::now() - start_all > wall_budget) {
                break;
            }
        }
        ns_samples.resize(filled);

        const auto wall1 = std::chrono::steady_clock::now();
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        if (hub != nullptr) bpf_post = hub->read();
#endif
        const uint64_t freq_end = detail::read_cpu_freq_hz(pinned_cpu.raw());

        // Drift: compare first-half vs second-half p50 as a proxy for
        // frequency or cache-state transitions during the run. Use
        // (N+1)/2 so that on odd N, h1 contains the median element and
        // is never shorter than h2 — makes the comparison symmetric in
        // "which half includes the middle sample?" rather than biased
        // toward the lower half (which N/2 would do).
        //
        // N is ns_samples.size(), not the requested S — when the
        // wall-time cap breaks the sample loop early, ns_samples is
        // shorter than S and using S here would read past the end.
        double drift = 0;
        bool drift_flag = false;
        const size_t N = ns_samples.size();
        if (N >= 200) {
            const auto half = static_cast<std::ptrdiff_t>((N + 1) / 2);
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

        // Cycles/op derived from rdtsc mean ns and TSC frequency.
        // Consistent with how "cyc" was reported before, independent of
        // PMU availability.
        if (const double tsc_hz = Timer::tsc_freq_hz(); tsc_hz > 0.0) {
            r.cycles_per_op = r.pct.mean * tsc_hz / 1e9;
        }

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        if (hub != nullptr) {
            r.bpf_delta    = bpf_post - bpf_pre;
            // attached_programs() returns Refined<bounded_above<64>, size_t>
            // post-GAPS-004a — the type carries a structural ≤64 bound that
            // matches inplace_vector<bpf_link*, 64>'s capacity.  Unwrap with
            // .value() for the raw count expected by the bench Report
            // struct (kept as plain size_t for printf-friendly emission).
            r.bpf_attached = hub->attached_programs().value();
        }
#endif

        return r;
    }

 private:
    enum class Pin : uint8_t { Auto, Explicit, None };

    std::string name_;
    size_t      samples_  = 0;          // TODO: strong type (count, not an id)
    size_t      warmup_   = 10'000;     // TODO: strong type
    size_t      batch_    = 0;          // TODO: strong type
    CpuId       core_{};                // CpuId::none() → falls through to Auto
    Pin         pin_mode_ = Pin::Auto;

    // Default wall-time budget per Run. Caps any bench where the body
    // × 100'000 samples × 10'000 warmup would run for hours or hold
    // gigabytes of short-lived allocations that glibc's heap pool
    // can't promptly return to the OS. Override globally via
    // CRUCIBLE_BENCH_WALL_MS; per-Run via .max_wall_ms(ms); disable
    // entirely via .max_wall_ms(0) or CRUCIBLE_BENCH_WALL_MS=0.
    static constexpr size_t kDefaultMaxWallMs = 10'000;
    size_t      max_wall_ms_   = kDefaultMaxWallMs;
    bool        have_max_wall_ = false;   // true iff .max_wall_ms() was called

    // CRUCIBLE_BENCH_WALL_MS=<n>. Empty/unset → default applies.
    [[nodiscard]] static std::optional<size_t> env_wall_ms_() noexcept {
        const char* s = std::getenv("CRUCIBLE_BENCH_WALL_MS");
        if (s == nullptr || s[0] == '\0') return std::nullopt;
        char* end = nullptr;
        const long v = std::strtol(s, &end, 10);
        if (end == s || v < 0) return std::nullopt;
        return static_cast<size_t>(v);
    }

    // Optional realtime policy applied for the duration of measure().
    // When set, crucible::rt::apply() is invoked at the top of measure()
    // and the returned AppliedPolicy is held until measure() returns —
    // scheduler / affinity / mlocks revert automatically. `.core()` and
    // `.no_pin()` are overridden by the policy's own CoreSelector when
    // hardening is set (the policy's Topology-based pick wins).
    crucible::rt::Policy hardening_{crucible::rt::Policy::none()};
    bool                 have_hardening_ = false;

    // CRUCIBLE_BENCH_HARDENING=production|cloud_vm|dev_quiet|none —
    // applies the named profile to every Run that doesn't call
    // .hardening() itself. Unset → fall through to legacy pin_() (no
    // hardening).
    [[nodiscard]] static std::optional<crucible::rt::Policy> env_hardening_() noexcept {
        const char* s = std::getenv("CRUCIBLE_BENCH_HARDENING");
        if (s == nullptr || s[0] == '\0') return std::nullopt;
        if (std::strcmp(s, "production") == 0) return crucible::rt::Policy::production();
        if (std::strcmp(s, "cloud_vm") == 0 ||
            std::strcmp(s, "vm")       == 0 ||
            std::strcmp(s, "cloud")    == 0) return crucible::rt::Policy::cloud_vm();
        if (std::strcmp(s, "dev_quiet") == 0 ||
            std::strcmp(s, "dev")       == 0) return crucible::rt::Policy::dev_quiet();
        if (std::strcmp(s, "none") == 0)      return crucible::rt::Policy::none();
        return std::nullopt;
    }

    [[nodiscard]] static size_t env_samples_() noexcept {
        if (const char* s = std::getenv("CRUCIBLE_BENCH_SAMPLES")) {
            const long n = std::strtol(s, nullptr, 10);
            if (n > 0) return static_cast<size_t>(n);
        }
        return 100'000;
    }

    // Apply affinity and return the CPU we actually ended up on.
    //
    //  • Pin::None → no affinity change, just report sched_getcpu().
    //  • Pin::Explicit with core_ >= 0 → target = core_.
    //  • Pin::Explicit with core_ < 0 (e.g. `.core(-1)` because the
    //    caller's env var defaulted to -1) → fall through to the Auto
    //    discovery path instead of silently skipping pinning.
    //  • Pin::Auto → hand off to `rt::select_hot_cpu`, which honors
    //    isolcpus, prefers P-cores, and (crucially) steers away from
    //    cpu0 and its SMT sibling because cpu0 absorbs timer IRQs /
    //    RCU callbacks on most Linux configs. Falls back to
    //    sched_getcpu() only if the selector returns -1 (empty
    //    allowed set).
    //
    // After a successful sched_setaffinity, re-read sched_getcpu() — the
    // scheduler is not obliged to migrate us synchronously, so the
    // value we return is the one actually executing user code. On
    // sched_setaffinity failure we return CpuId::none() rather than
    // the pre-call sched_getcpu(), to avoid silently pretending we
    // successfully pinned.
    [[nodiscard]] CpuId pin_() const noexcept {
#ifdef __linux__
        if (pin_mode_ == Pin::None) return CpuId{sched_getcpu()};

        int target = -1;
        const bool explicit_valid =
            (pin_mode_ == Pin::Explicit) && core_.is_valid();
        if (explicit_valid) {
            target = core_.raw();
        } else {
            // Auto path, OR Explicit-but-negative.
            //
            // Use the rt topology-aware selector. Same heuristic the
            // Keeper's Policy::apply() uses: isolcpu first, P-core
            // preference, avoid cpu0 and its SMT sibling (timer-tick
            // IRQ landing pad).
            target = crucible::rt::select_hot_cpu(crucible::rt::CoreSelector{});
            if (target < 0) target = sched_getcpu();
        }
        if (target < 0) return CpuId::none();

        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(target, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0) {
            return CpuId::none();  // fail loudly, not silently
        }
        // Re-read — scheduler may not have migrated us yet. This is a
        // benign race: `sched_getcpu()` reads the currently-executing
        // CPU, which, having just been restricted to {target} by
        // sched_setaffinity, must be `target` on the next schedule
        // boundary. On short paths (no preemption between setaffinity
        // and getcpu) we might still report the previous CPU — accept
        // that inaccuracy rather than calling sched_yield() and
        // burning a context switch inside every .measure().
        const int actual = sched_getcpu();
        return (actual >= 0) ? CpuId{actual} : CpuId{target};
#else
        return CpuId::none();
#endif
    }

    // Ramp batch size by 2× until the **body itself** (raw region minus
    // rdtsc overhead) exceeds 1000 cycles — keeps rdtsc overhead below
    // ~3 % of the measured region. Cap at 2^18.
    //
    // Older form compared `best` (includes overhead) to MIN_CYCLES, so
    // the terminator fired ~30 cycles early. Add `ovh` to the threshold
    // so the real body cost crosses 1000, not body+overhead.
    template <typename Body>
    [[nodiscard]] size_t auto_batch_(Body&& body) const {
        constexpr uint64_t MIN_CYCLES     = 1000;
        constexpr size_t   MAX_BATCH      = 1u << 18;
        constexpr size_t   PILOT_SAMPLES  = 100;

        const uint64_t ovh = Timer::overhead_cycles();

        size_t batch = 1;
        while (batch <= MAX_BATCH) {
            uint64_t best = UINT64_MAX;
            for (size_t i = 0; i < PILOT_SAMPLES; ++i) {
                const uint64_t t0 = rdtsc_start();
                for (size_t j = 0; j < batch; ++j) body();
                const uint64_t t1 = rdtsc_end();
                // Same wrap-safety argument as in the main loop.
                const uint64_t d  = t1 - t0;
                if (d < best) best = d;
            }
            if (best >= MIN_CYCLES + ovh) return batch;
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
    if (nspc == 0.0) {
        std::fprintf(stderr,
            "WARN bench: TSC calibration failed (ns_per_cycle == 0); "
            "cycles/op will read 0.\n");
    }

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    const bench::bpf::SenseHub* hub = detail::bpf_instance();
    if (hub != nullptr) {
        std::fprintf(out, "  BPF senses: loaded (%zu tracepoints attached)\n",
                     hub->attached_programs().value());
    } else {
        std::fprintf(out,
            "  BPF senses: UNAVAILABLE — set CRUCIBLE_BENCH_BPF_VERBOSE=1 for libbpf logs;\n"
            "              typical fix: sudo sysctl kernel.unprivileged_bpf_disabled=0\n"
            "              or `cmake --build --preset bench --target bench-caps`\n");
    }
#else
    std::fprintf(out, "  BPF senses: disabled at build time\n");
#endif

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

// ── Per-bench boilerplate helpers ──────────────────────────────────
//
// Every bench executable ends up duplicating:
//   - getenv-with-empty-check
//   - CRUCIBLE_BENCH_CORE → int parse (-1 if unset/invalid)
//   - CRUCIBLE_BENCH_JSON → bool
//   - a local `run = [&](...)` lambda that applies env-core to a new Run
//   - a JSON emission block after the reports array
//
// Pull those into the harness so every migration is the minimum needed.
// CRUCIBLE_BENCH_HARDENING is NOT parsed here — it's consumed by
// Run::measure() directly, so no bench-side work is required.

// Read an env var; return nullptr if unset OR empty (matches the convention
// of existing benches where an empty value is "unset").
[[nodiscard]] inline const char* env(const char* name) noexcept {
    const char* s = std::getenv(name);
    return (s != nullptr && s[0] != '\0') ? s : nullptr;
}

// Parse CRUCIBLE_BENCH_CORE. Returns -1 when unset, invalid, or outside
// the int range (the -1 sentinel is the "no explicit pin" signal that
// Run::pin_() falls back to Auto on).
[[nodiscard]] inline int env_core() noexcept {
    if (const char* s = env("CRUCIBLE_BENCH_CORE")) {
        char*      endp = nullptr;
        const long v    = std::strtol(s, &endp, 10);
        if (endp != s && v >= static_cast<long>(INT_MIN)
                      && v <= static_cast<long>(INT_MAX)) {
            return static_cast<int>(v);
        }
    }
    return -1;
}

// CRUCIBLE_BENCH_JSON=1 (or anything not "0") → emit JSON tail.
[[nodiscard]] inline bool env_json() noexcept {
    const char* s = env("CRUCIBLE_BENCH_JSON");
    return s != nullptr && std::strcmp(s, "0") != 0;
}

// One-shot: build a Run with env-driven core pinning and measure body.
// For benches that need .samples()/.warmup() overrides, construct Run
// directly and apply env_core() manually — see bench_arena.cpp slow-path
// case for the fluent-builder form.
template <typename Body>
[[nodiscard]] inline Report run(std::string name, Body&& body) {
    Run r{std::move(name)};
    if (const int c = env_core(); c >= 0) (void)r.core(c);
    return r.measure(std::forward<Body>(body));
}

// Emit the text block for every Report. Always done — the JSON path is
// additive. Taken as std::span so aggregate-init C arrays pass directly.
inline void emit_reports_text(std::span<const Report> reports, FILE* out = stdout) noexcept {
    for (const auto& r : reports) r.print_text(out);
}

// Emit the JSON array tail for every Report, iff `json`. No-op otherwise.
// Benches that want extra output between text and JSON (e.g., a bench::compare
// block) call emit_reports_text → extra prints → emit_reports_json.
inline void emit_reports_json(std::span<const Report> reports, bool json,
                              FILE* out = stdout) noexcept {
    if (!json) return;
    std::fputs("\n=== json ===\n[\n", out);
    for (size_t i = 0; i < reports.size(); ++i) {
        std::fputs("  ", out);
        reports[i].print_json(out);
        std::fputs((i + 1 < reports.size()) ? ",\n" : "\n", out);
    }
    std::fputs("]\n", out);
}

// Standard main() epilogue: text block + JSON tail, in that order. For
// benches that need anything between the two (compare/CI prints), call
// emit_reports_text + emit_reports_json directly.
inline void emit_reports(std::span<const Report> reports, bool json,
                         FILE* out = stdout) noexcept {
    emit_reports_text(reports, out);
    emit_reports_json(reports, json, out);
}

} // namespace bench
