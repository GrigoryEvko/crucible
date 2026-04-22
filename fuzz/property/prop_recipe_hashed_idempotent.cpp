// ═══════════════════════════════════════════════════════════════════
// prop_recipe_hashed_idempotent — hashed(hashed(r)) == hashed(r).
//
// The `hashed()` helper populates a NumericalRecipe's `hash` field
// via compute_recipe_hash.  compute_recipe_hash MUST exclude the
// `hash` field from its fold so that:
//
//   1. Re-hashing an already-hashed recipe gives the same value
//      (idempotence — load + re-hash on Cipher recovery doesn't drift)
//   2. Two recipes equal-modulo-hash-field receive equal hashes
//
// This is the single most load-bearing property of the recipe stack:
// a violation breaks Cipher round-trip determinism (CRUCIBLE.md §10).
//
// Catches:
//   - Future refactor that includes `hash` in compute_recipe_hash's
//     fold (e.g., "use reflect_hash<NumericalRecipe>" without a
//     local Spec exclusion)
//   - Padding-byte leakage that varies between fresh and re-hashed
//   - Compiler reorderings that perturb the computation between
//     successive calls
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/NumericalRecipe.h>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("compute_recipe_hash idempotence under hashed()", cfg,
        [](Rng& rng) { return random_recipe(rng); },
        [](const NumericalRecipe& r) {
            // Property 1: hash(fresh) == hash(after hashed()).
            //   The fresh recipe has hash=0; after hashed(), hash is
            //   populated.  compute_recipe_hash on either should yield
            //   the same value.
            const NumericalRecipe filled = hashed(r);
            if (compute_recipe_hash(r) != compute_recipe_hash(filled))
                return false;

            // Property 2: hashed(hashed(x)) == hashed(x).
            //   Triple-application stability — neither the hash field
            //   nor padding bits perturb the second pass.
            if (hashed(filled).hash != filled.hash) return false;

            // Property 3: poisoning the hash field doesn't affect
            // compute_recipe_hash.  Mutate `hash` to an arbitrary
            // value; the recomputed hash must match the original.
            NumericalRecipe poisoned = filled;
            poisoned.hash = RecipeHash{0xDEADBEEFCAFEBABEULL};
            if (compute_recipe_hash(poisoned) != filled.hash) return false;

            return true;
        });
}
