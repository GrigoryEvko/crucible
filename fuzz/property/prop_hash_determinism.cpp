// ═══════════════════════════════════════════════════════════════════
// prop_hash_determinism — same input → same hash, N trials.
//
// Catches:
//   - Static-local / process-global state in hash functions
//   - Compiler-introduced non-determinism (uninit reads, random
//     padding, etc.)
//   - ASLR-derived entropy leaking into Family-A hashes (a Family-A
//     hash that varies across calls would silently break Cipher
//     load-path identity)
//
// Strategy: for each iteration, generate a random input, hash it
// 8 times in a row, assert all 8 hashes match.  Multiple calls in
// the same iteration ensure no in-process drift; running across
// many iterations covers the input space.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Reflect.h>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("compute_recipe_hash determinism", cfg,
        [](Rng& rng) { return random_recipe(rng); },
        [](const NumericalRecipe& r) {
            const auto h0 = compute_recipe_hash(r);
            for (int k = 0; k < 8; ++k) {
                if (compute_recipe_hash(r) != h0) return false;
            }
            return true;
        });
}
