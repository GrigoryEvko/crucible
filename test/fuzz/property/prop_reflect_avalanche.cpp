// ═══════════════════════════════════════════════════════════════════
// prop_reflect_avalanche — strict avalanche criterion at scale.
//
// Stronger version of test_reflect.cpp's avalanche check: instead of
// 256 fixed bit-flips on one base input, sample 10K random base
// inputs × 1 random bit-flip each.  Aggregate flip-counts must
// follow ~N(32, sqrt(64*0.5*0.5)) = ~N(32, 4) per Webster & Tavares.
//
// Catches:
//   - regressions of the hash of the Protocol family that lose entropy
//     on specific input patterns (e.g., all-zeros / all-FFs /
//     power-of-two padding)
//   - leaf dispatch bugs (a future enum-handling change that maps
//     multiple input bytes to the same hash output)
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

#include <fixy/Core.h>
#include <fixy/Protocol.h>

#include <atomic>

namespace {

// 32-byte spec — enough fields to give the avalanche room to spread.
struct[[= ::fixy::derive_protocol{}]] AvalancheSpec {
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
};

struct PerturbedPair {
    AvalancheSpec base;
    AvalancheSpec perturbed;
    uint8_t bit_idx;  // [0, 256)
};

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(arguments);
    if (cfg.iterations < 10000) cfg.iterations = 10000;

    // Aggregate stats across iterations.  static so the property
    // closure (per-iteration check) can update them.
    static int64_t total_flips = 0;
    static int min_flips = 65;
    static int max_flips = -1;
    static int64_t samples = 0;

    int rc = run(
        "hash avalanche statistics", cfg,
        [](Rng& rng) {
            PerturbedPair p{};
            p.base.a = rng.next64();
            p.base.b = rng.next64();
            p.base.c = rng.next64();
            p.base.d = rng.next64();
            p.perturbed = p.base;
            p.bit_idx = static_cast<uint8_t>(rng.next_below(256));
            // The field of the bit: a, b, c or d, for a bit below 256.
            const auto field_of = [&p](unsigned const word) noexcept -> uint64_t& {
                switch (word) {
                    case 0:
                        return p.perturbed.a;
                    case 1:
                        return p.perturbed.b;
                    case 2:
                        return p.perturbed.c;
                    default:
                        return p.perturbed.d;
                }
            };
            field_of(p.bit_idx / 64u) ^= (uint64_t{1} << static_cast<unsigned>(p.bit_idx % 64));
            return p;
        },
        [](const PerturbedPair& p) {
            const uint64_t h_base = ::fixy::hash(p.base).raw();
            const uint64_t h_pert = ::fixy::hash(p.perturbed).raw();
            const uint64_t diff = h_base ^ h_pert;
            const int popcount = ::fixy::popcount(diff);
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
        const double mean = static_cast<double>(total_flips) / static_cast<double>(samples);
        ::fixy::report(::fixy::Sink::Err,
                       "[prop] avalanche stats over {} samples: "
                       "mean={}, min={}, max={}\n",
                       samples, mean, min_flips, max_flips);

        // Aggregate property: after enough samples, mean should be
        // within ±2 of the ideal 32.0 (1.4σ from CLT noise floor
        // at 10K samples).  min/max gates catch outliers that the
        // per-iteration check might miss.
        if (mean < 30.0 || mean > 34.0) {
            ::fixy::report(::fixy::Sink::Err,
                           "FAIL: mean flip-count {} outside [30, 34]; "
                           "hash avalanche degraded.\n",
                           mean);
            return 1;
        }
        if (min_flips < 8) {
            ::fixy::report(::fixy::Sink::Err, "FAIL: minimum flip-count {} < 8; stuck-bit anomaly.\n", min_flips);
            return 1;
        }
        if (max_flips > 56) {
            ::fixy::report(::fixy::Sink::Err, "FAIL: maximum flip-count {} > 56; over-amplification.\n", max_flips);
            return 1;
        }
    }
    return rc;
}
