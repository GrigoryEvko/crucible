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

#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

namespace names = crucible::recipe_names;

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv, 10000);  // setup-heavy

    return run(
        "RecipeRegistry by_name ↔ by_hash circle", cfg,
        [](Rng& rng) {
            // Generate one extra random recipe to test the
            // pool-but-not-registry branch.
            return random_recipe(rng);
        },
        [](const NumericalRecipe& extra) {
            ::fixy::BumpArena arena{};
            // The test context comes from foundation::effects::testing.
            // RecipePool takes an Init context at construction.
            // RecipeRegistry takes an Alloc capability, because it
            // interns the starter recipes into the pool at construction.
            const ::foundation::effects::Init init = ::foundation::effects::testing::init();
            RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
            RecipeRegistry registry{::fixy::mint_borrowed_ref(pool), init.alloc};

            // Phase 1: every starter name → canonical pointer →
            // by_hash recovers the same pointer.
            const ::fixy::TextView kNames[] = {
                names::kF32Strict,
                names::kF32Ordered,
                names::kF16F32AccumTc,
                names::kF16F32AccumOrdered,
                names::kBf16F32AccumTc,
                names::kBf16F32AccumOrdered,
                names::kFp8E4m3F32AccumMxOrd,
                names::kFp8E5m2F32AccumMxOrd,
            };
            for (auto n : kNames) {
                bool const is_round_trip = registry.by_name(n).match(
                    [&](RecipeRegistry::RecipeRef via_name) noexcept {
                        return registry.by_hash(via_name->hash)
                            .match(
                                [&](RecipeRegistry::RecipeRef via_hash) noexcept {
                                    return ::fixy::same_object(via_name, via_hash);
                                },
                                [](RecipeError) noexcept { return false; });
                    },
                    [](RecipeError) noexcept { return false; });
                if (!is_round_trip) return false;
            }

            // Phase 2: an extra random recipe interned via pool.
            // If its semantic fields happen to match a starter,
            // by_hash should hit.  Else it should miss with
            // HashNotFound, not NameNotFound.
            const auto* extra_ptr = pool.intern(init.alloc, extra);
            if (extra_ptr == nullptr) return false;
            return registry.by_hash(extra_ptr->hash)
                .match(
                    [&](RecipeRegistry::RecipeRef found) noexcept {
                        // A hit must be one of the starter recipes.
                        for (const auto& entry : registry.entries().value()) {
                            if (::fixy::same_object(entry.recipe, found)) return true;
                        }
                        return false;
                    },
                    // The by_hash error class is reserved for hash misses.
                    [](RecipeError error) noexcept { return error == RecipeError::HashNotFound; });
        });
}
