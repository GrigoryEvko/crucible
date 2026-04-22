// ═══════════════════════════════════════════════════════════════════
// prop_reflect_avalanche — strict avalanche criterion at scale.
//
// Stronger version of test_reflect.cpp's avalanche check: instead of
// 256 fixed bit-flips on one base input, sample 10K random base
// inputs × 1 random bit-flip each.  Aggregate flip-counts must
// follow ~N(32, sqrt(64*0.5*0.5)) = ~N(32, 4) per Webster & Tavares.
//
// Catches:
//   - reflect_hash regressions that lose entropy on specific input
//     patterns (e.g., all-zeros / all-FFs / power-of-two padding)
//   - hash_field type-conditional dispatch bugs (a future enum-handling
//     change that maps multiple input bytes to the same hash output)
//   - fmix64 mixing regressions that show up only on rare bit patterns
//
// Strategy: per iteration generate random AvalancheSpec, flip random
// bit, count output-bit flips.  Track running mean / min / max /
// histogram across iterations.  Property: the running mean across
// the sample must stay in [30, 34] after 10K iterations (very tight,
// statistically should be 32 ± 0.04 by CLT).  Min flip-count must
// be ≥ 8 (a flip producing < 8 output-bit changes is a stuck bit).
// Max ≤ 56 (similarly anomalous).
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/Reflect.h>

#include <atomic>
#include <cstdio>

namespace {

// 32-byte spec — enough fields to give the avalanche room to spread.
struct AvalancheSpec {
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
};

struct PerturbedPair {
    AvalancheSpec base;
    AvalancheSpec perturbed;
    uint8_t       bit_idx;   // [0, 256)
};

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    if (cfg.iterations < 10'000) cfg.iterations = 10'000;

    // Aggregate stats across iterations.  static so the property
    // closure (per-iteration check) can update them.
    static int64_t total_flips = 0;
    static int     min_flips   = 65;
    static int     max_flips   = -1;
    static int64_t samples     = 0;

    int rc = run("reflect_hash avalanche statistics", cfg,
        [](Rng& rng) {
            PerturbedPair p{};
            p.base.a = rng.next64();
            p.base.b = rng.next64();
            p.base.c = rng.next64();
            p.base.d = rng.next64();
            p.perturbed = p.base;
            p.bit_idx = static_cast<uint8_t>(rng.next_below(256));
            uint64_t* fields[4] = {&p.perturbed.a, &p.perturbed.b,
                                    &p.perturbed.c, &p.perturbed.d};
            *fields[p.bit_idx / 64] ^=
                (uint64_t{1} << static_cast<unsigned>(p.bit_idx % 64));
            return p;
        },
        [](const PerturbedPair& p) {
            const uint64_t h_base = reflect_hash(p.base);
            const uint64_t h_pert = reflect_hash(p.perturbed);
            const uint64_t diff   = h_base ^ h_pert;
            const int popcount = __builtin_popcountll(
                static_cast<unsigned long long>(diff));
            total_flips += popcount;
            if (popcount < min_flips) min_flips = popcount;
            if (popcount > max_flips) max_flips = popcount;
            ++samples;
            // Per-iteration stuck-bit guard: any flip of zero output
            // bits is a guaranteed stuck-bit bug (input changed,
            // output didn't).  Catches degenerate hash collapse.
            return popcount > 0;
        });

    if (rc == 0) {
        const double mean = static_cast<double>(total_flips) /
                            static_cast<double>(samples);
        std::fprintf(stderr,
            "[prop] avalanche stats over %lld samples: "
            "mean=%.3f, min=%d, max=%d\n",
            static_cast<long long>(samples), mean, min_flips, max_flips);

        // Aggregate property: after enough samples, mean should be
        // within ±2 of the ideal 32.0 (1.4σ from CLT noise floor
        // at 10K samples).  min/max gates catch outliers that the
        // per-iteration check might miss.
        if (mean < 30.0 || mean > 34.0) {
            std::fprintf(stderr,
                "FAIL: mean flip-count %.3f outside [30, 34]; "
                "reflect_hash avalanche degraded.\n", mean);
            return 1;
        }
        if (min_flips < 8) {
            std::fprintf(stderr,
                "FAIL: minimum flip-count %d < 8; stuck-bit anomaly.\n",
                min_flips);
            return 1;
        }
        if (max_flips > 56) {
            std::fprintf(stderr,
                "FAIL: maximum flip-count %d > 56; over-amplification.\n",
                max_flips);
            return 1;
        }
    }
    return rc;
}
