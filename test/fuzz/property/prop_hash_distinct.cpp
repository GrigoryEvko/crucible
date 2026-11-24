// ═══════════════════════════════════════════════════════════════════
// prop_hash_distinct — different inputs → different hashes.
//
// Catches:
//   - Field-N-not-folded-in bugs (e.g., a future refactor drops a
//     field from the fold; suddenly recipes differing only in that
//     field collide)
//   - Stuck-bit bugs in fmix64 that lose entropy on certain input
//     patterns, or a fold that flattens its chain into one xor and so
//     stops telling operand order apart
//   - Endianness / sign-extension bugs that map distinct enum values
//     to the same byte (we hit this earlier with ScalarType::Undefined)
//
// Strategy: for each iteration, generate two recipes that differ in
// exactly one field; assert their hashes differ.  Iterating across
// the fields and many random base recipes exercises every field's
// contribution to the hash.
//
// Probabilistic note: fmix64's avalanche means a 64-bit hash collision
// on two distinct inputs has ~2^-64 probability per pair.  For 100K
// iterations, chance of ANY false-positive collision is ~5e-15.
// Negligible; test treats any collision as a real bug.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/NumericalRecipe.h>
#include <fixy/Core.h>

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(arguments);

    return run(
        "compute_recipe_hash field-disambiguation", cfg,
        [](Rng& rng) {
            // Generate (base, perturbed) pair where perturbed differs
            // from base in exactly one of 8 fields.  Field choice is
            // random per iteration so all 8 fields get coverage.
            struct Pair {
                NumericalRecipe a;
                NumericalRecipe b;
                uint8_t field_idx;
            };
            Pair p{};
            p.a = random_recipe(rng);
            p.b = p.a;
            p.field_idx = static_cast<uint8_t>(rng.next_below(8));
            switch (p.field_idx) {
                case 0:
                    p.b.accum_dtype = next_enumerator(p.a.accum_dtype);
                    break;
                case 1:
                    p.b.out_dtype = next_enumerator(p.a.out_dtype);
                    break;
                case 2:
                    p.b.reduction_algo = next_enumerator(p.a.reduction_algo);
                    break;
                case 3:
                    p.b.rounding = next_enumerator(p.a.rounding);
                    break;
                case 4:
                    p.b.scale_policy = next_enumerator(p.a.scale_policy);
                    break;
                case 5:
                    p.b.softmax = next_enumerator(p.a.softmax);
                    break;
                case 6:
                    p.b.determinism = next_enumerator(p.a.determinism);
                    break;
                case 7:
                    p.b.flags = p.a.flags ^ decltype(p.a.flags){RecipeFlags::FLUSH_TO_ZERO};
                    break;
                default:
                    ::fixy::unreachable();
            }
            return p;
        },
        [](const auto& p) {
            // Distinct semantic fields → distinct hashes.
            return compute_recipe_hash(p.a) != compute_recipe_hash(p.b);
        });
}
