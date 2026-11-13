// ═══════════════════════════════════════════════════════════════════
// prop_philox_simd_equivalence — randomized bit-equivalence check
// of the SIMD Philox batch against the scalar Philox.
//
// Property:
//   For every (counter, key) octuple drawn at random, the SIMD
//   philox_batch8 produces output bit-identical to scalar
//   fixy::Philox4x32::generate on every one of its 8 lanes.
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

#include <crucible/PhiloxSimd.h>
#include <fixy/Core.h>
#include <foundation/Simd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

// The vector of the word Word of each of the eight blocks, lane by lane.
// The generator constructor is the only way to materialize a vec lane by
// lane (operator[] is value-returning const).
template <std::size_t Word, class Blocks>
[[nodiscard]] foundation::simd::u32x8 lanes_of_word(Blocks const& blocks) noexcept {
    return foundation::simd::u32x8(
        [&](auto lane) noexcept -> uint32_t { return blocks[decltype(lane)::value].at(::fixy::index<Word>()); });
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::detail;
    using namespace crucible::fuzz::prop;

    const Config cfg = parse_args(argc, argv);

    return run(
        "Philox SIMD bit-equivalence", cfg,
        [](Rng& rng) {
            // 8 octuples of (counter, key) = 8 × (4 + 2) = 48 u32 values.
            struct Inputs {
                std::array<::fixy::Philox4x32::Counter, 8> counters;
                std::array<::fixy::Philox4x32::Key, 8> keys;
            };
            Inputs inputs{};
            for (std::size_t i = 0; i < 8; ++i) {
                inputs.counters[i] = {rng.next32(), rng.next32(), rng.next32(), rng.next32()};
                inputs.keys[i] = {rng.next32(), rng.next32()};
            }
            return inputs;
        },
        [](const auto& inputs) {
            using foundation::simd::u32x8;

            // Build SoA inputs from the AoS octuple.
            const u32x8 ctr0 = lanes_of_word<0>(inputs.counters);
            const u32x8 ctr1 = lanes_of_word<1>(inputs.counters);
            const u32x8 ctr2 = lanes_of_word<2>(inputs.counters);
            const u32x8 ctr3 = lanes_of_word<3>(inputs.counters);
            const u32x8 key0 = lanes_of_word<0>(inputs.keys);
            const u32x8 key1 = lanes_of_word<1>(inputs.keys);

            const auto batch = philox_batch8(ctr0, ctr1, ctr2, ctr3, key0, key1);

            // For each lane, compute the scalar oracle and assert
            // bit-identical match across all 4 output words.
            for (std::size_t lane = 0; lane < 8; ++lane) {
                const auto& counter = inputs.counters[lane];
                const auto& key = inputs.keys[lane];
                const auto scalar = ::fixy::Philox4x32::generate(counter, key);
                const int li = static_cast<int>(lane);
                const uint32_t word0 = scalar.at(::fixy::index<0>());
                const uint32_t word1 = scalar.at(::fixy::index<1>());
                const uint32_t word2 = scalar.at(::fixy::index<2>());
                const uint32_t word3 = scalar.at(::fixy::index<3>());

                if (batch.r0[li] != word0 || batch.r1[li] != word1 || batch.r2[li] != word2 || batch.r3[li] != word3) {
                    std::fprintf(stderr,
                                 "\n[lane %zu] SIMD/scalar divergence:\n"
                                 "  ctr  = {%08x, %08x, %08x, %08x}\n"
                                 "  key  = {%08x, %08x}\n"
                                 "  scalar: %08x %08x %08x %08x\n"
                                 "  simd:   %08x %08x %08x %08x\n",
                                 lane, counter.at(::fixy::index<0>()), counter.at(::fixy::index<1>()),
                                 counter.at(::fixy::index<2>()), counter.at(::fixy::index<3>()),
                                 key.at(::fixy::index<0>()), key.at(::fixy::index<1>()), word0, word1, word2, word3,
                                 batch.r0[li], batch.r1[li], batch.r2[li], batch.r3[li]);
                    return false;
                }
            }
            return true;
        });
}
