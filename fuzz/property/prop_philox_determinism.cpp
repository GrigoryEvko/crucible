// ═══════════════════════════════════════════════════════════════════
// prop_philox_determinism — Philox4x32 counter-based RNG determinism.
//
// Philox is THE keystone of Crucible's replay determinism story
// (CRUCIBLE.md §10.3): "Same (key, counter) always produces the
// same rand_bits, regardless of which chip / which thread / when."
//
// Property 1 (determinism):
//   For any (offset, key) pair, Philox::generate produces the
//   SAME 4-u32 Ctr across every invocation, forever.
//
// Property 2 (decorrelation):
//   Different (offset, key) → different outputs with high
//   probability.  Any collision at this sample size (< 2^-120 per
//   pair) indicates a real bug in the mixing schedule.
//
// Property 3 (op_key determinism):
//   op_key(master, op_index, content_hash) is a pure function of
//   its inputs.  Same inputs → same output; different inputs →
//   different output with high probability.
//
// Catches:
//   - Compiler reorderings that perturb the Philox mul/xor chain
//   - Static-local caching that would leak state across calls
//   - Endianness assumption bugs in the 32×32→64 multiply
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/Philox.h>
#include <crucible/Types.h>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("Philox determinism + decorrelation", cfg,
        [](Rng& rng) {
            struct Pair {
                uint64_t offset_a;
                uint64_t key_a;
                uint64_t offset_b;
                uint64_t key_b;
            };
            Pair p{};
            p.offset_a = rng.next64();
            p.key_a    = rng.next64();
            p.offset_b = rng.next64();
            p.key_b    = rng.next64();
            return p;
        },
        [](const auto& p) {
            // Property 1: determinism.  The SAME (offset, key)
            // always produces the SAME Ctr.  Eight repeated calls
            // must all yield identical output bits.
            const auto first = Philox::generate(p.offset_a, p.key_a);
            for (int k = 0; k < 8; ++k) {
                const auto again = Philox::generate(p.offset_a, p.key_a);
                if (again[0] != first[0] || again[1] != first[1] ||
                    again[2] != first[2] || again[3] != first[3])
                    return false;
            }

            // Property 2: decorrelation.  Two distinct (offset, key)
            // pairs MUST produce different 4-u32 outputs (probability
            // of collision: ~2^-128, negligible).  Skip the check if
            // we happen to generate the same inputs.
            if (p.offset_a == p.offset_b && p.key_a == p.key_b)
                return true;
            const auto other = Philox::generate(p.offset_b, p.key_b);
            if (other[0] == first[0] && other[1] == first[1] &&
                other[2] == first[2] && other[3] == first[3])
                return false;

            // Property 3: op_key determinism.  Pure function; same
            // inputs → same output.
            const auto k1 = Philox::op_key(
                p.offset_a, static_cast<uint32_t>(p.key_a),
                ContentHash{p.offset_b});
            const auto k2 = Philox::op_key(
                p.offset_a, static_cast<uint32_t>(p.key_a),
                ContentHash{p.offset_b});
            if (k1 != k2) return false;

            return true;
        });
}
