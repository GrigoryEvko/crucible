// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-093 HS14 fixture 1/2.  Recipe-tier-mismatch rejection.
//
// `fixy::fp::canonicalize_for<R>(double)` static_asserts on
// `is_bitexact(R.determinism)` — only BITEXACT_TC and BITEXACT_STRICT
// recipes promise the cross-vendor bit-stability that merkle hashing
// depends on.  A recipe declared with `ReductionDeterminism::ORDERED`
// (the production default for non-bitexact training) gives multi-ULP
// cross-vendor drift; folding its scalars into merkle would lock
// content_hash to a non-portable bit pattern.
//
// Mismatch class for HS14 audit: recipe-determinism-tier rejection
// (distinct from the rounding-mode fixture's RoundingMode::RN gate —
// both static_asserts protect the SAME merkle-stability invariant
// but at orthogonal recipe axes: determinism tier covers reduction-
// algorithm cross-vendor agreement; rounding mode covers default-
// kernel realization).
//
// Architectural intent: the four-tier determinism ladder (UNORDERED
// / ORDERED / BITEXACT_TC / BITEXACT_STRICT, see
// NumericalRecipe.h:121 + FORGE.md §19.1) is intentionally graded so
// each tier admits a strictly larger set of cross-vendor-stable
// operations.  Merkle hashing is the strictest operation in the
// system (any one ULP of drift ⟹ cache miss ⟹ recompile);
// canonicalize_for therefore demands the top two tiers and rejects
// the bottom two at compile time.
//
// Concrete bug-class this catches: a contributor writes a recipe for
// the `ATTN_FP16_ORDERED` family (correct for inference) and tries
// to merkle-fold one of its scale factors into a region content_hash
// — the recipe is fine for inference but folds non-portably into
// merkle.  Caught at the canonicalize_for instantiation site rather
// than days later when a cross-vendor CI run notices content_hash
// drift.
//
// Expected diagnostic: "BITEXACT_TC or BITEXACT_STRICT" OR
// "is_bitexact" OR "static assertion failed".

#include <crucible/fixy/fp/Canonicalize.h>
#include <crucible/NumericalRecipe.h>

namespace fp = crucible::fixy::fp;

// `ORDERED` is the production default for mixed-vendor non-bitexact
// training (per NumericalRecipe.h:103-105) — well-defined, useful,
// and NOT admissible for merkle folding.  RN rounding is correct
// here (passes the orthogonal gate) so ONLY the determinism-tier
// static_assert fires; orthogonality demonstration, not accidental
// co-coverage with the rounding-mode fixture.
constexpr fp::CanonicalizeRecipeSpec kOrderedSpec{
    crucible::RoundingMode::RN,                       // RN is fine
    crucible::ReductionDeterminism::ORDERED,          // NOT bitexact
};

int main() {
    // The static_assert on `is_bitexact(Spec.determinism)` in
    // canonicalize_for<Spec> fires at template-instantiation time.
    auto bad = fp::canonicalize_for<kOrderedSpec>(1.0);
    (void)bad;
    return 0;
}
