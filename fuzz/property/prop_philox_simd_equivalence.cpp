// ═══════════════════════════════════════════════════════════════════
// prop_philox_simd_equivalence — randomized bit-equivalence proof
// for SIMD-9.
//
// Property:
//   For every (counter, key) octuple drawn at random, the SIMD
//   philox_batch8 produces output bit-identical to scalar
//   Philox::generate on every one of its 8 lanes.
//
// What this catches that the unit test in test_philox_simd does not:
//
//   * Adversarial inputs the unit test forgot.  100k iterations
//     with full-32-bit counters and keys covers the value space far
//     beyond what hand-crafted edge cases reach.
//   * Cross-lane contamination that only appears under specific
//     inter-lane bit patterns (e.g., a particular permutation of M0
//     × ctr that lights up a wraparound in lane k but not lane j).
//   * Compiler reorderings that perturb the cast-multiply-shift
//     idiom — would manifest as one or more lanes diverging from
//     the scalar oracle.
//   * Future GCC/libstdc++ updates that change std::simd's
//     converting constructor or shift behavior — would surface as
//     a CI red the moment new code hits the regression suite.
//
// ─── How it works ───────────────────────────────────────────────────
//
// Each iteration draws 8 × (4 × u32 counter, 2 × u32 key) octuples
// from the Philox-derived Rng, runs both the SIMD batch and the
// scalar oracle once per lane, and asserts every word matches.
// Failure prints the failing lane + the inputs that triggered it,
// reproducible exactly via --seed=N --iters=M.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/Philox.h>
#include <crucible/PhiloxSimd.h>
#include <crucible/safety/Simd.h>

#include <array>
#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::detail;
    using namespace crucible::fuzz::prop;

    const Config cfg = parse_args(argc, argv);

    return run("Philox SIMD bit-equivalence", cfg,
        [](Rng& rng) {
            // 8 octuples of (counter, key) = 8 × (4 + 2) = 48 u32 values.
            struct Inputs {
                std::array<Philox::Ctr, 8> counters;
                std::array<Philox::Key, 8> keys;
            };
            Inputs inputs{};
            for (std::size_t i = 0; i < 8; ++i) {
                inputs.counters[i] = {
                    rng.next32(), rng.next32(),
                    rng.next32(), rng.next32()};
                inputs.keys[i] = {rng.next32(), rng.next32()};
            }
            return inputs;
        },
        [](const auto& inputs) {
            using simd::u32x8;

            // Build SoA inputs from the AoS octuple.  The generator
            // constructor is the only way to materialize a vec
            // lane-by-lane (operator[] is value-returning const).
            auto build_ctr_word = [&](std::size_t word) {
                return u32x8([&](auto lane) noexcept -> uint32_t {
                    return inputs.counters[decltype(lane)::value][word];
                });
            };
            auto build_key_word = [&](std::size_t word) {
                return u32x8([&](auto lane) noexcept -> uint32_t {
                    return inputs.keys[decltype(lane)::value][word];
                });
            };

            const u32x8 ctr0 = build_ctr_word(0);
            const u32x8 ctr1 = build_ctr_word(1);
            const u32x8 ctr2 = build_ctr_word(2);
            const u32x8 ctr3 = build_ctr_word(3);
            const u32x8 key0 = build_key_word(0);
            const u32x8 key1 = build_key_word(1);

            const auto batch =
                philox_batch8(ctr0, ctr1, ctr2, ctr3, key0, key1);

            // For each lane, compute the scalar oracle and assert
            // bit-identical match across all 4 output words.
            for (std::size_t lane = 0; lane < 8; ++lane) {
                const auto scalar = Philox::generate(
                    inputs.counters[lane], inputs.keys[lane]);
                const int li = static_cast<int>(lane);

                if (batch.r0[li] != scalar[0] ||
                    batch.r1[li] != scalar[1] ||
                    batch.r2[li] != scalar[2] ||
                    batch.r3[li] != scalar[3])
                {
                    std::fprintf(stderr,
                        "\n[lane %zu] SIMD/scalar divergence:\n"
                        "  ctr  = {%08x, %08x, %08x, %08x}\n"
                        "  key  = {%08x, %08x}\n"
                        "  scalar: %08x %08x %08x %08x\n"
                        "  simd:   %08x %08x %08x %08x\n",
                        lane,
                        inputs.counters[lane][0], inputs.counters[lane][1],
                        inputs.counters[lane][2], inputs.counters[lane][3],
                        inputs.keys[lane][0], inputs.keys[lane][1],
                        scalar[0], scalar[1], scalar[2], scalar[3],
                        batch.r0[li], batch.r1[li],
                        batch.r2[li], batch.r3[li]);
                    return false;
                }
            }
            return true;
        });
}
