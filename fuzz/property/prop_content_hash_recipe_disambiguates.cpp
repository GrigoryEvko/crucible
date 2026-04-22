// ═══════════════════════════════════════════════════════════════════
// prop_content_hash_recipe_disambiguates — Layer 3c integration test
// at scale.
//
// Property: for any random ops sequence and two semantically distinct
// recipes A, B:
//
//   compute_content_hash(ops, &A) != compute_content_hash(ops, &B)
//   compute_content_hash(ops, nullptr) != compute_content_hash(ops, &A)
//
// This is THE load-bearing safety property the REFL-3c wire-in
// established: KernelCache lookups must NOT collide across recipes.
// A regression here silently breaks replay determinism.
//
// Catches:
//   - Future refactor that drops the recipe fold from
//     compute_content_hash (callers would silently lose the
//     disambiguation; KernelCache would serve cross-recipe kernels)
//   - Hash-fold ordering bugs that make the recipe contribution
//     XOR-cancel against ops contribution at edge inputs
//
// Strategy: per iteration, generate (ops, recipe_a, recipe_b) where
// recipe_a and recipe_b are semantically distinct.  Verify the three
// content_hash inequalities.  100K iterations exhaustively sample
// the (ops, recipe_a, recipe_b) cross-product space.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>

#include <array>
#include <span>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("compute_content_hash recipe disambiguation", cfg,
        [](Rng& rng) {
            constexpr unsigned MAX = 8;
            struct Triple {
                std::array<TraceEntry, MAX> ops;
                uint8_t                     count;
                NumericalRecipe             recipe_a;
                NumericalRecipe             recipe_b;
            };
            Triple t{};
            t.count = static_cast<uint8_t>(rng.next_below(MAX) + 1);
            for (uint8_t i = 0; i < t.count; ++i) {
                t.ops[i] = TraceEntry{};
                t.ops[i].schema_hash = SchemaHash{rng.next64()};
            }
            t.recipe_a = hashed(random_recipe(rng));
            // Generate recipe_b until it semantically differs from
            // recipe_a (with random inputs, this almost always passes
            // on the first try; the loop is defensive).
            do {
                t.recipe_b = hashed(random_recipe(rng));
            } while (t.recipe_b.hash == t.recipe_a.hash);
            return t;
        },
        [](const auto& t) {
            const std::span<const TraceEntry> ops_span{
                t.ops.data(), t.count};

            const auto h_none = compute_content_hash(ops_span);
            const auto h_a    = compute_content_hash(ops_span, &t.recipe_a);
            const auto h_b    = compute_content_hash(ops_span, &t.recipe_b);

            // Three pairwise inequalities — the cross-recipe-cache-
            // collision-prevention contract.
            if (h_a == h_b)    return false;
            if (h_a == h_none) return false;
            if (h_b == h_none) return false;
            return true;
        });
}
