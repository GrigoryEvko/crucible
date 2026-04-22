// ═══════════════════════════════════════════════════════════════════
// prop_recipe_registry_roundtrip — by_name → by_hash circle.
//
// Property: for every starter recipe name N:
//   r1 = registry.by_name(N)
//   r2 = registry.by_hash(r1->hash)
//   r1 == r2 (same canonical pointer)
//
// Plus the inverse for arbitrary interned recipes:
//   pool.intern(fields) = r1
//   registry.by_hash(r1->hash) — may miss (recipe not in registry)
//   but if it hits, must be r1
//
// Catches:
//   - Future refactor that breaks the by_name/by_hash equivalence
//     (e.g., a hash-table-style by_hash that misses on collisions)
//   - HashNotFound vs NameNotFound classification regressions
//   - Cipher-load-path safety: the by_hash recovery path must
//     resolve to the exact same canonical pointer that the
//     persisted recipe came from
//
// Strategy: per iteration, build a registry, then sweep every
// starter name + a few random pool-only intern attempts.  Both
// directions of the round-trip must hold.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>

namespace names = crucible::recipe_names;

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 10'000) cfg.iterations = 10'000;  // setup-heavy

    return run("RecipeRegistry by_name ↔ by_hash circle", cfg,
        [](Rng& rng) {
            // Generate one extra random recipe to test the
            // pool-but-not-registry branch.
            return random_recipe(rng);
        },
        [](const NumericalRecipe& extra) {
            Arena arena{};
            fx::Test test{};
            RecipePool pool{arena, test.alloc};
            RecipeRegistry registry{pool, test.alloc};

            // Phase 1: every starter name → canonical pointer →
            // by_hash recovers the same pointer.
            const std::string_view kNames[] = {
                names::kF32Strict, names::kF32Ordered,
                names::kF16F32AccumTc, names::kF16F32AccumOrdered,
                names::kBf16F32AccumTc, names::kBf16F32AccumOrdered,
                names::kFp8E4m3F32AccumMxOrd,
                names::kFp8E5m2F32AccumMxOrd,
            };
            for (auto n : kNames) {
                auto via_name = registry.by_name(n);
                if (!via_name.has_value()) return false;

                auto via_hash = registry.by_hash((*via_name)->hash);
                if (!via_hash.has_value()) return false;
                if (*via_name != *via_hash) return false;
            }

            // Phase 2: an extra random recipe interned via pool.
            // If its semantic fields happen to match a starter,
            // by_hash should hit; otherwise it should miss with
            // HashNotFound (NOT NameNotFound).
            const auto* extra_ptr = pool.intern(test.alloc, extra);
            if (extra_ptr == nullptr) return false;
            auto via_hash_extra = registry.by_hash(extra_ptr->hash);
            if (via_hash_extra.has_value()) {
                // Hit — must be one of the starter recipes.
                bool found = false;
                for (const auto& entry : registry.entries()) {
                    if (entry.recipe == *via_hash_extra) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            } else {
                // Miss — must be classified as HashNotFound, not
                // NameNotFound (the by_hash error class is reserved
                // for hash misses; name misses use NameNotFound).
                if (via_hash_extra.error() != RecipeError::HashNotFound)
                    return false;
            }
            return true;
        });
}
