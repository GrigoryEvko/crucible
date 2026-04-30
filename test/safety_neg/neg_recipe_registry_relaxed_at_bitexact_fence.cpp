// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// THE LOAD-BEARING REJECTION FOR FOUND-G04 (NumericalTier production
// call site).  RecipeRegistry::by_name_pinned<RELAXED>(name) returns
// NumericalTier<RELAXED, const NumericalRecipe*> for an ORDERED or
// UNORDERED recipe.  A consumer that classifies itself as BITEXACT-
// fence-tolerating REQUIRES at-least-BITEXACT-tier hardware-friendliness
// from its producers.  An ORDERED-emitting site (e.g., a refactor that
// substituted memory_order_seq_cst → memory_order_acquire on a
// reduction's atomic accumulator AND demoted from BITEXACT_STRICT to
// ORDERED to "make a perf regression go away") MUST be rejected at
// the BITEXACT-fence boundary — ORDERED is the WEAKEST tolerance
// claim short of UNORDERED (≤4 ULP cross-vendor at the storage
// dtype's precision; not bit-identical), and silently accepting it
// at a BITEXACT-required site (CPU oracle compliance, replay-
// determinism CI, deterministic-training contract) collapses the
// numerics CI's invariant from 0 ULP to ≤4 ULP — a 4× degradation
// past the contract.
//
// Lattice direction (ToleranceLattice.h):
//     RELAXED(weakest) ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑ ULP_FP32
//                     ⊑ ULP_FP64 ⊑ BITEXACT(strongest)
//
// satisfies<Required> = leq(Required, Self).  For RELAXED to satisfy
// BITEXACT, we'd need leq(BITEXACT, RELAXED) — but BITEXACT is
// STRICTLY HIGHER (more numerically tight) than RELAXED, so
// leq(BITEXACT, RELAXED) is FALSE.  The requires-clause rejects.
//
// Concrete bug-class this catches: a refactor adds a recipe with
// `ReductionDeterminism::ORDERED` and a TC-style accumulator (the
// well-known "≤4 ULP is good enough for everyone" assumption).  The
// recipe's tolerance_of() returns RELAXED.  EVERY hot-path BITEXACT-
// consumer's `requires satisfies<BITEXACT>` rejects it at compile
// time, naming the failed predicate.  The bug never reaches main;
// the test reddens at the PR.  This is the canonical "tolerance
// downgrade silently flowed into BITEXACT-tier consumer" class —
// today caught by careful review or by cross-vendor numerics CI 12
// hours later (≤4 ULP disagreement); with the wrapper, caught at
// the type level the moment the recipe is pulled.
//
// Companion to the BITEXACT_TC fixture (one-tier rejection): this is
// the THREE-tier-gap rejection — ORDERED → RELAXED is THREE tiers
// below BITEXACT_TC's ULP_FP* class.  Strongest possible rejection
// claim across the 7-tier lattice for a starter recipe.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/NumericalTier.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: BITEXACT-fence admission gate.  Models
// the CPU-oracle / deterministic-replay / cross-vendor numerics CI
// pattern that FOUND-G04 production sites flow into.
template <typename W>
    requires (W::template satisfies<Tolerance::BITEXACT>)
static int bitexact_fence_consumer(W wrapped) noexcept {
    (void) std::move(wrapped).consume();
    return 0;
}

int main() {
    // Pinned at RELAXED — origin is a recipe with ORDERED or UNORDERED
    // determinism (≤4 ULP at storage precision; not bit-identical).
    // This is what hot-path BITEXACT-required call sites MUST reject;
    // RELAXED on a deterministic-training site is the canonical
    // "tolerance downgrade silently flowed in" regression class.
    NumericalTier<Tolerance::RELAXED, int> relaxed_value{42};

    // Should FAIL: bitexact_fence_consumer requires satisfies<BITEXACT>;
    // RELAXED is STRICTLY WEAKER (in the tolerance lattice) than
    // BITEXACT, so the constraint fails.  Without this fence, an
    // ORDERED-pinned recipe would silently flow into the BITEXACT-
    // contract path, breaking the 0-ULP invariant on every step.
    int result = bitexact_fence_consumer(std::move(relaxed_value));
    return result;
}
