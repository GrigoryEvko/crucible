// ═══════════════════════════════════════════════════════════════════
// prop_hash_distinct — different inputs → different hashes.
//
// Catches:
//   - Field-N-not-folded-in bugs (e.g., a future refactor drops a
//     field from the fold; suddenly recipes differing only in that
//     field collide)
//   - Stuck-bit bugs in fmix64 / wymix that lose entropy on certain
//     input patterns
//   - Endianness / sign-extension bugs that map distinct enum values
//     to the same byte (we hit this earlier with ScalarType::Undefined)
//
// Strategy: for each iteration, generate two recipes that differ in
// EXACTLY ONE field; assert their hashes differ.  Iterating across
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

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("compute_recipe_hash field-disambiguation", cfg,
        [](Rng& rng) {
            // Generate (base, perturbed) pair where perturbed differs
            // from base in exactly one of 8 fields.  Field choice is
            // random per iteration so all 8 fields get coverage.
            struct Pair {
                NumericalRecipe a;
                NumericalRecipe b;
                uint8_t         field_idx;
            };
            Pair p{};
            p.a = random_recipe(rng);
            p.b = p.a;
            p.field_idx = static_cast<uint8_t>(rng.next_below(8));
            switch (p.field_idx) {
                case 0: p.b.accum_dtype    =
                    static_cast<ScalarType>(static_cast<int8_t>(p.a.accum_dtype) ^ 1); break;
                case 1: p.b.out_dtype      =
                    static_cast<ScalarType>(static_cast<int8_t>(p.a.out_dtype) ^ 1); break;
                case 2: p.b.reduction_algo =
                    static_cast<ReductionAlgo>(
                        (static_cast<uint8_t>(p.a.reduction_algo) + 1) & 3);
                    break;
                case 3: p.b.rounding       =
                    static_cast<RoundingMode>(
                        (static_cast<uint8_t>(p.a.rounding) + 1) & 3);
                    break;
                case 4: p.b.scale_policy   =
                    static_cast<ScalePolicy>(
                        (static_cast<uint8_t>(p.a.scale_policy) + 1) % 6);
                    break;
                case 5: p.b.softmax        =
                    static_cast<SoftmaxRecurrence>(
                        (static_cast<uint8_t>(p.a.softmax) + 1) & 3);
                    break;
                case 6: p.b.determinism    =
                    static_cast<ReductionDeterminism>(
                        (static_cast<uint8_t>(p.a.determinism) + 1) & 3);
                    break;
                case 7: p.b.flags          = static_cast<uint8_t>(p.a.flags ^ 1); break;
                default: std::unreachable();
            }
            return p;
        },
        [](const auto& p) {
            // Distinct semantic fields → distinct hashes.
            return compute_recipe_hash(p.a) != compute_recipe_hash(p.b);
        });
}
