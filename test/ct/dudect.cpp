// The dudect timing harness for the constant-time primitives.
//
// For each primitive, the harness measures the time of one call with two
// classes of input.  Class 0 is one fixed input.  Class 1 is a random
// input.  The class of each measurement is random.  Welch's t-test then
// compares the two timing distributions, over all measurements and over
// the measurements below each of 100 percentile limits, as dudect does.
// A value of |t| below 4.5 gives no evidence of a leak.  A value above 10
// shows a leak.
//
// The harness is evidence, not a gate: a timing result depends on the
// host, its load and its clock.  Run it on an isolated core:
//
//   taskset -c 90 build/test/ct/ct_dudect [measurements]
//
// A leaking control runs first.  It compares bytes with an early exit,
// and the harness exits 1 when the control does not show a leak, because
// a harness that sees nothing proves nothing.

#include <fixy/ConstantTime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

#include <sched.h>
#include <x86intrin.h>

namespace {

constexpr std::size_t kBatch = std::size_t{1} << 16;
constexpr std::size_t kPercentiles = 100;
constexpr std::size_t kBytes = 32;
constexpr double kNoEvidence = 4.5;
constexpr double kLeak = 10.0;

// A deterministic xorshift generator.  The harness needs random classes
// and random inputs, not a cryptographic source.
struct Xorshift {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;

    // Return the next 64 random bits.
    std::uint64_t next() noexcept {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
};

// Welford's online mean and variance for the two classes, and Welch's t
// over them.
struct Welch {
    std::array<double, 2> mean{};
    std::array<double, 2> m2{};
    std::array<double, 2> count{};

    // Add one measurement of class `cls`.
    void push(double value, std::size_t cls) noexcept {
        count[cls] += 1.0;
        double const delta = value - mean[cls];
        mean[cls] += delta / count[cls];
        m2[cls] += delta * (value - mean[cls]);
    }

    // Return Welch's t, or 0 when a class has fewer than two measurements.
    [[nodiscard]] double t() const noexcept {
        if (count[0] < 2.0 || count[1] < 2.0) return 0.0;
        double const var0 = m2[0] / (count[0] - 1.0);
        double const var1 = m2[1] / (count[1] - 1.0);
        double const den = std::sqrt(var0 / count[0] + var1 / count[1]);
        return den > 0.0 ? (mean[0] - mean[1]) / den : 0.0;
    }
};

// The inputs of one call: two 64-bit operands, a selection bit and two
// byte buffers.  Each primitive reads the fields it needs.
struct Input {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    std::uint64_t bit = 0;
    std::array<std::byte, kBytes> left{};
    std::array<std::byte, kBytes> right{};
};

// Fill `input` for class 0 (fixed) or class 1 (random).
void fill(Input& input, std::size_t cls, Xorshift& rng) noexcept {
    if (cls == 0) {
        input = Input{};
        return;
    }
    input.a = rng.next();
    input.b = rng.next();
    input.bit = rng.next() & 1u;
    for (std::size_t i = 0; i < kBytes; ++i) input.left[i] = std::byte{0};
    for (std::size_t i = 0; i < kBytes; ++i) input.right[i] = static_cast<std::byte>(rng.next());
}

// The wrappers under test.  Each is out of line and reads its operands
// from memory, so the compiler cannot specialize a call on its class.
using Probe = std::uint64_t (*)(Input const&) noexcept;

template <typename Ct>
[[gnu::noinline]] std::uint64_t probe_mask_from_bit(Input const& in) noexcept {
    return Ct::mask_from_bit(in.bit);
}
template <typename Ct>
[[gnu::noinline]] std::uint64_t probe_select(Input const& in) noexcept {
    return Ct::select(in.bit, in.a, in.b);
}
template <typename Ct>
[[gnu::noinline]] std::uint64_t probe_less(Input const& in) noexcept {
    return Ct::less(in.a, in.b);
}
template <typename Ct>
[[gnu::noinline]] std::uint64_t probe_is_zero(Input const& in) noexcept {
    return Ct::is_zero(in.a);
}
template <typename Ct>
[[gnu::noinline]] std::uint64_t probe_cswap(Input const& in) noexcept {
    std::uint64_t left = in.a;
    std::uint64_t right = in.b;
    Ct::cswap(in.bit, left, right);
    return left ^ (right << 1);
}
template <typename Ct>
[[gnu::noinline]] std::uint64_t probe_eq(Input const& in) noexcept {
    return Ct::eq(std::span<const std::byte>{in.left}, std::span<const std::byte>{in.right}) ? 1u : 0u;
}
[[gnu::noinline]] std::uint64_t probe_fixy_eq_static(Input const& in) noexcept {
    return ::fixy::ct::eq(std::span<const std::byte, kBytes>{in.left}, std::span<const std::byte, kBytes>{in.right})
               ? 1u
               : 0u;
}

// The leaking control: a byte comparison that stops at the first
// difference.  Class 0 compares equal buffers to the end, and class 1
// stops early.
[[gnu::noinline]] std::uint64_t probe_control_early_exit(Input const& in) noexcept {
    for (std::size_t i = 0; i < kBytes; ++i) {
        if (in.left[i] != in.right[i]) return 0;
    }
    return 1;
}

// The fixy::ct primitives at 64 bits, spelled for the probes.
struct FixyCt {
    static std::uint64_t mask_from_bit(std::uint64_t bit) noexcept { return ::fixy::ct::mask_from_bit(bit); }
    static std::uint64_t select(std::uint64_t bit, std::uint64_t a, std::uint64_t b) noexcept {
        return ::fixy::ct::select(bit, a, b);
    }
    static std::uint64_t less(std::uint64_t a, std::uint64_t b) noexcept { return ::fixy::ct::less(a, b); }
    static std::uint64_t is_zero(std::uint64_t x) noexcept { return ::fixy::ct::is_zero(x); }
    static void cswap(std::uint64_t bit, std::uint64_t& a, std::uint64_t& b) noexcept { ::fixy::ct::cswap(bit, a, b); }
    static bool eq(std::span<const std::byte> a, std::span<const std::byte> b) noexcept { return ::fixy::ct::eq(a, b); }
};

struct Target {
    std::string_view name;
    Probe probe;
};

constexpr std::array kTargets{
    Target{"fixy mask_from_bit", probe_mask_from_bit<FixyCt>},
    Target{"fixy select", probe_select<FixyCt>},
    Target{"fixy less", probe_less<FixyCt>},
    Target{"fixy is_zero", probe_is_zero<FixyCt>},
    Target{"fixy cswap", probe_cswap<FixyCt>},
    Target{"fixy eq, 32 bytes", probe_eq<FixyCt>},
    Target{"fixy eq, static 32 bytes", probe_fixy_eq_static},
};

// Time one batch of calls.  The classes and the inputs are ready before
// the first timed call.
void measure_batch(Probe probe, std::vector<Input> const& inputs, std::vector<double>& cycles) noexcept {
    volatile std::uint64_t sink = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        unsigned aux = 0;
        _mm_lfence();
        std::uint64_t const start = __rdtsc();
        _mm_lfence();
        sink = sink ^ probe(inputs[i]);
        std::uint64_t const stop = __rdtscp(&aux);
        _mm_lfence();
        cycles[i] = static_cast<double>(stop - start);
    }
}

struct Result {
    double max_abs_t = 0.0;
    std::size_t crop = 0;  // 0 is no crop, k is the k-th percentile limit.
    double samples = 0.0;
};

// Run `measurements` timed calls of one probe and return the largest |t|
// over the uncropped test and the 100 cropped tests.  O(measurements).
Result run_target(Probe probe, std::size_t measurements) {
    Xorshift rng;
    std::vector<Input> inputs(kBatch);
    std::vector<std::size_t> classes(kBatch);
    std::vector<double> cycles(kBatch);
    std::array<double, kPercentiles> limits{};
    std::array<Welch, kPercentiles + 1> tests{};

    auto prepare = [&] {
        for (std::size_t i = 0; i < kBatch; ++i) {
            classes[i] = rng.next() & 1u;
            fill(inputs[i], classes[i], rng);
        }
    };

    // The first batch warms the caches and sets the percentile limits.
    prepare();
    measure_batch(probe, inputs, cycles);
    std::vector<double> sorted = cycles;
    std::ranges::sort(sorted);
    for (std::size_t k = 0; k < kPercentiles; ++k) {
        double const fraction = 1.0 - std::pow(0.5, 10.0 * static_cast<double>(k + 1) / kPercentiles);
        limits[k] = sorted[static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1))];
    }

    for (std::size_t done = 0; done < measurements; done += kBatch) {
        prepare();
        measure_batch(probe, inputs, cycles);
        for (std::size_t i = 0; i < kBatch; ++i) {
            tests[0].push(cycles[i], classes[i]);
            for (std::size_t k = 0; k < kPercentiles; ++k) {
                if (cycles[i] < limits[k]) tests[k + 1].push(cycles[i], classes[i]);
            }
        }
    }

    Result result;
    for (std::size_t k = 0; k < tests.size(); ++k) {
        double const magnitude = std::abs(tests[k].t());
        if (magnitude > result.max_abs_t) {
            result.max_abs_t = magnitude;
            result.crop = k;
            result.samples = tests[k].count[0] + tests[k].count[1];
        }
    }
    return result;
}

// Name the verdict for one |t|.
std::string_view verdict(double max_abs_t) noexcept {
    if (max_abs_t < kNoEvidence) return "no evidence of a leak";
    if (max_abs_t < kLeak) return "possible leak";
    return "leak";
}

// Print one line of the report.
void report(std::string_view name, Result const& result) noexcept {
    std::printf("%-28.*s max |t| = %8.2f  crop %3zu  samples %10.0f  %.*s\n", static_cast<int>(name.size()),
                name.data(), result.max_abs_t, result.crop, result.samples, static_cast<int>(verdict(result.max_abs_t).size()),
                verdict(result.max_abs_t).data());
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t measurements = std::size_t{1} << 24;
    if (argc == 2) measurements = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (argc > 2 || measurements < kBatch) {
        std::fprintf(stderr, "usage: ct_dudect [measurements, at least %zu]\n", kBatch);
        return 2;
    }
    std::printf("dudect: %zu measurements per primitive on cpu %d, fixed class against random class\n", measurements,
                sched_getcpu());

    Result const control = run_target(probe_control_early_exit, measurements);
    report("control, early-exit compare", control);
    if (control.max_abs_t < kLeak) {
        std::fputs("dudect: the leaking control shows no leak, so the harness is blind\n", stderr);
        return 1;
    }

    int leaks = 0;
    for (Target const& target : kTargets) {
        Result const result = run_target(target.probe, measurements);
        report(target.name, result);
        if (result.max_abs_t >= kNoEvidence) ++leaks;
    }
    std::printf("dudect: %d of %zu primitives show |t| >= %.1f\n", leaks, kTargets.size(), kNoEvidence);
    return 0;
}
