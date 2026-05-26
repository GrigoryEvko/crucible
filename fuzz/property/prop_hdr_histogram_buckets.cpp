// ═══════════════════════════════════════════════════════════════════
// prop_hdr_histogram_buckets.cpp — bucket-arithmetic + percentile-
// monotonicity fuzzer for observe::HdrHistogram (observe/HdrHistogram.h).
//
// HdrHistogram is a fixed-shape high-dynamic-range histogram: the
// recording hot path is `counts_[counts_index(value)].fetch_add(1)`
// with NO runtime bounds check — record() trusts the layout math
// (HdrHistogram.h:158).  So `counts_index(v) < bucket_slots` for every
// admissible value is a MEMORY-SAFETY invariant, not just a correctness
// one: a layout-math regression that lets the index escape the array is
// an out-of-bounds atomic write into the Keeper's metric storage.
//
// The existing test (test_hdr_histogram.cpp) is thorough on BEHAVIORS —
// zero handling, concurrency/publish discipline, merge/subtract,
// serialize, multi-tag isolation — but the core BUCKET ARITHMETIC is
// only spot-checked: it records ~8 hand-picked values {0,10,20,100,500,
// 777,1000,1000000} and asserts coarse `percentile() >= / <=` bounds at
// a handful of points.  It never sweeps the full [0, MaxValue] range,
// never exercises an alternate (Significant, MaxValue) layout, and never
// pins percentile monotonicity.  This fuzzer closes that gap.
//
// ── Two property runs ──────────────────────────────────────────────
//
// Run A (layout arithmetic, four layouts).  The reference is the inverse
// function value_from_index — a DIFFERENT computation from counts_index
// (encode vs decode), so a divergence is a genuine bug.  For each random
// value v ∈ [0, MaxValue], log-uniform across magnitudes so every HDR
// bucket is hit densely, it asserts:
//   * counts_index(v) < bucket_slots          (MemSafe — record() trusts it)
//   * value_from_index(idx) <= v              (bucket-low never exceeds value)
//   * counts_index(value_from_index(idx)) == idx   (decode∘encode idempotent)
//   * v < value_from_index(idx+1)             (v sits strictly below the next
//                                              slot — pins v into exactly one
//                                              bucket; catches any off-by-one
//                                              AND proves value_from_index is
//                                              strictly increasing in index,
//                                              the premise percentile() relies
//                                              on for monotonicity)
//
// Run B (percentile monotonicity, one layout).  Records a small random
// batch, then asserts:
//   * percentile(p) is non-decreasing as p increases (rank-based query
//     must never invert — would surface a non-monotone value_from_index
//     or a cumulative-count off-by-one)
//   * percentile(100) == value_from_index(counts_index(max_recorded)) —
//     the exact bucket-low of the largest sample (p100 lands in the top
//     non-empty bucket)
//   * percentile(100) <= MaxValue
//
// Both verified clean by hand-trace before shipping (counts_index ≤
// counts_len-1 for all v ≤ MaxValue; the two-regime value_from_index
// inverts both the bucket-0 raw-index path and the bucket-b≥1 path), so
// this is the regression net the spot-test lacked, not a bug report.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/observe/HdrHistogram.h>

#include <array>
#include <cstdint>

namespace {

using crucible::fuzz::prop::Rng;

// Log-uniform sample in [0, max_value]: pick a magnitude (bit width)
// uniformly, then a uniform value within it.  This hits every HDR
// magnitude bucket densely rather than concentrating mass at the top
// few bits the way a flat next64()%(max+1) would.
[[nodiscard]] std::uint64_t log_uniform_below_or_eq(Rng& rng,
                                                    std::uint64_t max_value) noexcept {
    const std::uint32_t kind = rng.next_below(8u);
    if (kind == 0u) return 0u;
    if (kind == 1u) return 1u;
    if (kind == 2u) return max_value;
    const std::uint32_t bits = rng.next_below(64u);
    const std::uint64_t raw = bits == 0u ? 0u : (rng.next64() >> (64u - bits));
    return raw > max_value ? (raw % (max_value + 1u)) : raw;
}

// Run A — pure layout-arithmetic invariants for one HdrHistogram shape.
template <typename Hist>
[[nodiscard]] int run_layout(const char* name,
                             crucible::fuzz::prop::Config cfg) {
    using Layout = typename Hist::layout_type;
    constexpr std::uint64_t kMax = Hist::max_trackable_value;
    constexpr std::size_t kSlots = Hist::bucket_slots;

    return crucible::fuzz::prop::run(name, cfg,
        [](Rng& rng) noexcept -> std::uint64_t {
            return log_uniform_below_or_eq(rng, kMax);
        },
        [](const std::uint64_t& value) noexcept -> bool {
            const std::size_t idx = Layout::counts_index(value);
            // MemSafe: record() does counts_[idx].fetch_add with no guard.
            if (idx >= kSlots) return false;
            const std::uint64_t low = Layout::value_from_index(idx);
            // Bucket low must never exceed the value that selected it.
            if (low > value) return false;
            // decode ∘ encode is idempotent: the bucket-low maps to the
            // same slot the original value did.
            if (Layout::counts_index(low) != idx) return false;
            // The value sits strictly below the next slot's low — pins it
            // into exactly one bucket and proves value_from_index strictly
            // increases across this slot boundary.
            if (idx + 1 < kSlots) {
                const std::uint64_t next_low = Layout::value_from_index(idx + 1);
                if (!(value < next_low)) return false;
            }
            return true;
        });
}

inline constexpr std::size_t kMaxBatch = 32;

struct BatchSpec {
    std::array<std::uint64_t, kMaxBatch> values{};
    std::uint32_t count = 0;
    std::uint8_t pad[4]{};
};

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    namespace obs = crucible::observe;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    // ── Run A: layout arithmetic across four distinct shapes ──
    // Distinct (Significant, MaxValue) pairs exercise different
    // sub_bucket_count / magnitude / bucket_count so the bucketing math
    // is validated beyond the single <2, 1'000'000> the test uses.
    int rc = 0;
    rc |= run_layout<obs::HdrHistogram<1, 1'000>>("hdr_layout_s1_1e3", cfg);
    rc |= run_layout<obs::HdrHistogram<2, 1'000'000>>("hdr_layout_s2_1e6", cfg);
    rc |= run_layout<obs::HdrHistogram<3, 3'600'000'000'000ull>>(
        "hdr_layout_s3_3p6e12", cfg);
    rc |= run_layout<obs::HdrHistogram<5, 100'000>>("hdr_layout_s5_1e5", cfg);

    // ── Run B: percentile monotonicity + exact p100 on one layout ──
    using Hist = obs::HdrHistogram<2, 1'000'000>;
    using Layout = Hist::layout_type;
    constexpr std::uint64_t kMax = Hist::max_trackable_value;

    // Keep histogram-construction-per-iteration affordable on deep runs.
    Config batch_cfg = cfg;
    if (batch_cfg.iterations > 100'000) batch_cfg.iterations = 100'000;

    rc |= run("hdr_percentile_monotonic", batch_cfg,
        [](Rng& rng) noexcept -> BatchSpec {
            BatchSpec spec{};
            spec.count = 1u + rng.next_below(static_cast<std::uint32_t>(kMaxBatch - 1));
            for (std::uint32_t i = 0; i < spec.count; ++i) {
                spec.values[i] = log_uniform_below_or_eq(rng, kMax);
            }
            return spec;
        },
        [](const BatchSpec& spec) noexcept -> bool {
            Hist hist;
            std::uint64_t max_recorded = 0;
            for (std::uint32_t i = 0; i < spec.count; ++i) {
                hist.record(Hist::checked_value(spec.values[i]));
                if (spec.values[i] > max_recorded) max_recorded = spec.values[i];
            }
            if (hist.total_count() != spec.count) return false;

            // Percentile must be non-decreasing as pct climbs.
            constexpr std::array<double, 10> points{
                0.5, 1.0, 5.0, 25.0, 50.0, 75.0, 90.0, 99.0, 99.9, 100.0};
            std::uint64_t previous = 0;
            for (const double pct : points) {
                const std::uint64_t quantile = hist.percentile(pct);
                if (quantile < previous) return false;
                previous = quantile;
            }

            // p100 lands in the top non-empty bucket — exactly the
            // bucket-low of the largest recorded sample.
            const std::uint64_t expected_p100 =
                Layout::value_from_index(Layout::counts_index(max_recorded));
            if (hist.percentile(100.0) != expected_p100) return false;
            if (hist.percentile(100.0) > kMax) return false;
            return true;
        });

    return rc;
}
