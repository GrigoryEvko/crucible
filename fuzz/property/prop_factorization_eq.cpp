// ═══════════════════════════════════════════════════════════════════
// prop_factorization_eq.cpp — differential-oracle fuzzer for
// decide::factorization_eq (safety/Decide.h).
//
// factorization_eq returns true iff the product of a factor list
// equals `total`, REJECTING on any intermediate multiply overflow.
// It is the CONTRACT-110 gate: a 5D-parallelism decomposition
// (TP × DP × PP × EP × CP) must multiply to world_size, and the
// Meridian discrete-search partition optimizer relies on it to reject
// nonsensical factorizations.  A false-accept silently mis-shards the
// model across devices; a false-reject blocks a valid partition.  Its
// step-by-step __builtin_mul_overflow logic has a subtle contract — it
// rejects on ANY intermediate overflow even when a later 0 factor
// would mathematically rescue the product to 0 — so it is worth fuzzing
// to pin that semantic against future refactors.  Today it has only
// hand-picked static_assert coverage.
//
// The oracle is an INDEPENDENT mechanism: it accumulates the running
// product in uint64_t (wide enough that a uint32 × uint32 step never
// overflows the accumulator) and checks the uint32 range AT EACH STEP.
// That mirrors factorization_eq's stepwise-overflow contract EXACTLY —
// including the {overflow-then-zero} corner, where both break at the
// overflowing step before reaching the 0 — but via a different
// computation (a 64-bit widen + explicit compare, versus the
// __builtin_mul_overflow intrinsic).  A naive compute-the-whole-
// product-then-check oracle would DISAGREE on that corner; this one
// agrees on every input, so a divergence is a genuine bug (e.g. a
// future "optimization" that loses the stepwise check), not an
// artifact of the oracle.
//
// Four generator modes give teeth-by-construction:
//   * ExactFactorization — factors built so the running product stays
//     in range; total = that product.  MUST return true.
//   * OffByFactor        — a valid factorization with total perturbed
//     (XOR 1, so total != product).  MUST return false.
//   * Overflow           — >= 2 factors each >= 65536, so the product
//     provably exceeds UINT32_MAX at the second step.  MUST return
//     false.
//   * Random             — arbitrary factors (incl. 0 / 1 / large) and
//     arbitrary total; the stepwise oracle is ground truth.
//
// Every iteration asserts the universal differential (cut == oracle)
// plus the construction-guaranteed direction.  ExactFactorization
// (true) versus OffByFactor/Overflow (false) are opposite-direction,
// so a vacuous oracle cannot satisfy the whole suite.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/safety/Decide.h>

#include <array>
#include <cstdint>
#include <span>

namespace {

inline constexpr uint32_t kMaxFactors = 12;
inline constexpr uint64_t kU32Max     = 0xFFFF'FFFFull;

enum class Mode : uint8_t {
    ExactFactorization = 0,
    OffByFactor        = 1,
    Overflow           = 2,
    Random             = 3,
};

struct FactorSpec {
    std::array<uint32_t, kMaxFactors> factors{};
    uint32_t total = 0;
    uint32_t count = 0;
    Mode     mode  = Mode::Random;
    uint8_t  pad[3]{};
};

using crucible::fuzz::prop::Rng;

// Independent oracle: stepwise product in uint64_t, mirroring the
// uint32 stepwise-overflow contract of factorization_eq<uint32_t> via
// a 64-bit widen + range compare (not __builtin_mul_overflow).  The
// running value is always <= UINT32_MAX when it enters a step, and a
// factor is <= UINT32_MAX, so `running * factor` <= UINT32_MAX² which
// fits uint64_t — the accumulator itself never overflows.
[[nodiscard]] bool oracle(const FactorSpec& spec) noexcept {
    uint64_t running = 1;
    for (uint32_t e = 0; e < spec.count; ++e) {
        const uint64_t step = running * static_cast<uint64_t>(spec.factors[e]);
        if (step > kU32Max) {
            return false;  // mirrors __builtin_mul_overflow firing on uint32
        }
        running = step;
    }
    return running == static_cast<uint64_t>(spec.total);
}

// Build factors incrementally so the running product stays in
// [1, UINT32_MAX]; returns the product.  Each factor is in
// [1, min(headroom, 256)] so products grow gently and many factors fit.
[[nodiscard]] uint64_t build_in_range(
    Rng& rng, std::array<uint32_t, kMaxFactors>& out, uint32_t& count) noexcept
{
    uint64_t running = 1;
    const uint32_t target = rng.next_below(kMaxFactors + 1u);  // [0, kMaxFactors]
    count = 0;
    for (uint32_t n = 0; n < target; ++n) {
        const uint64_t headroom = kU32Max / running;  // max factor keeping product in range
        if (headroom < 1u) break;
        const uint32_t cap = static_cast<uint32_t>(headroom < 256u ? headroom : 256u);
        const uint32_t f = 1u + rng.next_below(cap);  // [1, cap]
        out[count++] = f;
        running *= f;
    }
    return running;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    using crucible::decide::factorization_eq;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("factorization_eq", cfg,
        // ── Generator: one of four factor-list shapes ──
        [](Rng& rng) noexcept -> FactorSpec {
            FactorSpec spec{};
            spec.mode = static_cast<Mode>(rng.next_below(4));
            switch (spec.mode) {
                case Mode::ExactFactorization: {
                    const uint64_t product = build_in_range(rng, spec.factors, spec.count);
                    spec.total = static_cast<uint32_t>(product);
                    break;
                }
                case Mode::OffByFactor: {
                    const uint64_t product = build_in_range(rng, spec.factors, spec.count);
                    // XOR 1 flips the low bit → guaranteed != product,
                    // stays within uint32.
                    spec.total = static_cast<uint32_t>(product) ^ 1u;
                    break;
                }
                case Mode::Overflow: {
                    // >= 2 factors each >= 65536: the product exceeds
                    // UINT32_MAX at the second step (65536² == 2³²).
                    spec.count = 2u + rng.next_below(kMaxFactors - 1u);  // [2, kMaxFactors]
                    for (uint32_t e = 0; e < spec.count; ++e) {
                        spec.factors[e] = 65536u + rng.next_below(34464u);  // [65536, 99999]
                    }
                    spec.total = rng.next32();  // arbitrary; overflow rejects regardless
                    break;
                }
                case Mode::Random: {
                    spec.count = rng.next_below(kMaxFactors + 1u);  // [0, kMaxFactors]
                    for (uint32_t e = 0; e < spec.count; ++e) {
                        spec.factors[e] = rng.next_below(70000u);  // incl. 0, 1, overflow-capable
                    }
                    spec.total = rng.next32();
                    break;
                }
                default: std::unreachable();  // mode ∈ {0..3} by next_below(4)
            }
            return spec;
        },
        // ── Property: differential + construction-direction ──
        [](const FactorSpec& spec) noexcept -> bool {
            const std::span<const uint32_t> view{spec.factors.data(), spec.count};
            const bool cut   = factorization_eq<uint32_t>(view, spec.total);
            const bool truth = oracle(spec);

            // Universal: code-under-test agrees with the independent
            // stepwise-uint64 oracle on EVERY input.
            if (cut != truth) return false;

            switch (spec.mode) {
                case Mode::ExactFactorization:
                    if (!cut) return false;  // product == total by construction
                    break;
                case Mode::OffByFactor:
                case Mode::Overflow:
                    if (cut) return false;   // perturbed/overflowing → must reject
                    break;
                case Mode::Random:
                    break;                   // oracle is ground truth
                default: std::unreachable();
            }
            return true;
        });
}
