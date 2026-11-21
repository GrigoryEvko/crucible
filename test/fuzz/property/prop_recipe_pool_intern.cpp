// ═══════════════════════════════════════════════════════════════════
// prop_recipe_pool_intern — Swiss-table intern invariant under load.
//
// The intern pool's load-bearing property:
//   pool.intern(a) == pool.intern(b)   iff   semantic_equal(a, b)
//
// This must hold for EVERY pair of recipes inserted into a pool, no
// matter the insertion order, no matter how many grows occurred.
//
// Catches:
//   - Swiss-table grow_() bugs (rehash collisions, off-by-one in
//     probe order)
//   - Hash-comparison shortcuts that skip semantic_equal_
//   - Pool-storage corruption when arena reuses memory across grows
//   - Pointer-identity drift across the load factor crossover
//
// Strategy: build one pool, insert M random recipes, then for EACH
// pair (i, j) check the iff invariant.  M chosen to span ~3 grows.
// Per iteration the recipe set is randomized.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

namespace {

// M = 30 spans 3 grows from initial cap 8 (8 → 16 → 32 → 64).
constexpr unsigned kRecipes = 30;

using Recipes = ::fixy::FixedArray<crucible::NumericalRecipe, kRecipes>;

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(arguments, 5000);  // O(M²) inner

    return run(
        "RecipePool intern iff semantic equal", cfg,
        [](Rng& rng) {
            Recipes recipes{};
            recipes.with_mut([&rng](auto run) noexcept {
                for (auto& r : run)
                    r = random_recipe(rng);
            });
            return recipes;
        },
        [](const Recipes& recipes) {
            ::fixy::BumpArena arena{};
            // The test context comes from foundation::effects::testing.
            // RecipePool takes an Init context, because it allocates its
            // slot table at construction.  Each intern() call takes an
            // Alloc capability.
            const ::foundation::effects::Init init = ::foundation::effects::testing::init();
            RecipePool pool{::fixy::to(arena), init,
                            /*initial_capacity=*/8};

            // Phase 1: insert all, keep a Ref of each recipe.
            ::fixy::FixedList<::fixy::Ref<const NumericalRecipe>, kRecipes> interned{};
            for (const NumericalRecipe& recipe : recipes) {
                const ::fixy::Ref<const NumericalRecipe> stored = pool.intern(init.alloc, recipe);
                // Pool authority: returned recipe carries the
                // canonical compute_recipe_hash, ignoring any
                // pre-set hash on the input.
                if (stored->hash != compute_recipe_hash(recipe)) return false;
                static_cast<void>(interned.append(stored).expect("the list holds one Ref for each recipe"));
            }

            // Phase 2: pairwise iff invariant.  semantic_equal is
            // the same predicate the pool uses internally; mirror
            // it here so the test enforces the contract from outside.
            auto sem_eq = [](const NumericalRecipe& a, const NumericalRecipe& b) noexcept {
                return a.accum_dtype == b.accum_dtype && a.out_dtype == b.out_dtype
                    && a.reduction_algo == b.reduction_algo && a.rounding == b.rounding
                    && a.scale_policy == b.scale_policy && a.softmax == b.softmax && a.determinism == b.determinism
                    && a.flags == b.flags;
            };
            for (const ::fixy::Index<kRecipes> i : recipes.indices()) {
                for (const ::fixy::Index<kRecipes> j : recipes.indices()) {
                    const bool is_same_recipe =
                        ::fixy::same_object(*interned.get(i.raw()).expect("each recipe has a Ref"),
                                            *interned.get(j.raw()).expect("each recipe has a Ref"));
                    const bool eq_fields = sem_eq(recipes.at(i), recipes.at(j));
                    if (is_same_recipe != eq_fields) return false;
                }
            }
            return true;
        });
}
