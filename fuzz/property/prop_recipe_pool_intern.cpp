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

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>

#include <array>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 5'000) cfg.iterations = 5'000;  // O(M²) inner

    return run("RecipePool intern iff semantic equal", cfg,
        [](Rng& rng) {
            // M = 30 spans 3 grows from initial cap 8 (8 → 16 → 32 → 64).
            constexpr unsigned M = 30;
            std::array<NumericalRecipe, M> recipes{};
            for (auto& r : recipes) r = random_recipe(rng);
            return recipes;
        },
        [](const std::array<NumericalRecipe, 30>& recipes) {
            Arena arena{};
            fx::Test test{};
            RecipePool pool{arena, test.alloc, /*initial_capacity=*/8};

            // Phase 1: insert all, capture pointers.
            std::array<const NumericalRecipe*, 30> ptrs{};
            for (size_t i = 0; i < recipes.size(); ++i) {
                ptrs[i] = pool.intern(test.alloc, recipes[i]);
                if (ptrs[i] == nullptr) return false;        // never null
                // Pool authority: returned recipe carries the
                // canonical compute_recipe_hash, ignoring any
                // pre-set hash on the input.
                if (ptrs[i]->hash != compute_recipe_hash(recipes[i]))
                    return false;
            }

            // Phase 2: pairwise iff invariant.  semantic_equal is
            // the same predicate the pool uses internally; mirror
            // it here so the test enforces the contract from outside.
            auto sem_eq = [](const NumericalRecipe& a,
                              const NumericalRecipe& b) noexcept {
                return a.accum_dtype    == b.accum_dtype
                    && a.out_dtype      == b.out_dtype
                    && a.reduction_algo == b.reduction_algo
                    && a.rounding       == b.rounding
                    && a.scale_policy   == b.scale_policy
                    && a.softmax        == b.softmax
                    && a.determinism    == b.determinism
                    && a.flags          == b.flags;
            };
            for (size_t i = 0; i < recipes.size(); ++i) {
                for (size_t j = 0; j < recipes.size(); ++j) {
                    const bool eq_ptrs   = (ptrs[i] == ptrs[j]);
                    const bool eq_fields = sem_eq(recipes[i], recipes[j]);
                    if (eq_ptrs != eq_fields) return false;
                }
            }
            return true;
        });
}
