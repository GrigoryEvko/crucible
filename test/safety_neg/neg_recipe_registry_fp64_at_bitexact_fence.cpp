// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Adjacent ONE-TIER-GAP rejection.  Companion to the load-bearing
// FOUND-G04 fixture (neg_recipe_registry_relaxed_at_bitexact_fence —
// 6-tier-gap rejection): proves the consumer-fence rejection is
// STRUCTURAL across the lattice, not a "RELAXED-special-case."
//
// Violation: passing a `NumericalTier<ULP_FP64, T>` value to a function
// whose `requires` clause demands `NumericalTier::satisfies<BITEXACT>`.
// ULP_FP64 is one tier weaker than BITEXACT in the tolerance lattice
// (RELAXED ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑ ULP_FP32 ⊑ ULP_FP64
// ⊑ BITEXACT).  satisfies<BITEXACT> = leq(BITEXACT, ULP_FP64) = false.
//
// Concrete bug-class: a refactor downgrades a recipe from
// BITEXACT_STRICT (0 ULP) to BITEXACT_TC + FP64 storage (0-1 ULP at
// FP64 precision = ULP_FP64-class via tolerance_of()).  The change
// looks innocuous — "still bitexact at the FP64 ULP level, perf wins
// from TC fragments" — but the deterministic-replay consumer requires
// strictly BITEXACT (byte-identical across vendors), and ULP_FP64
// admits up to 1 ULP of cross-vendor variation at FP64 precision.
// On a 1B-step replay loop, that's 1B × 1 ULP of accumulated drift —
// the canonical "BITEXACT_TC crept onto a BITEXACT_STRICT-required
// site" regression.  Today caught by cross-vendor numerics CI 12
// hours later (≤1 ULP disagreement); with this wrapper, caught at
// the type level the moment the recipe is pulled.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/NumericalTier.h>

#include <utility>

using namespace crucible::safety;

template <typename W>
    requires (W::template satisfies<Tolerance::BITEXACT>)
static int bitexact_fence_consumer(W wrapped) noexcept {
    (void) std::move(wrapped).consume();
    return 0;
}

int main() {
    // Pinned at ULP_FP64 — origin is a BITEXACT_TC FP64-storage recipe
    // (0-1 ULP cross-vendor at FP64 precision).  Strictly stronger than
    // ORDERED but NOT bit-identical.
    NumericalTier<Tolerance::ULP_FP64, int> fp64_value{42};

    // Should FAIL: bitexact_fence_consumer requires satisfies<BITEXACT>;
    // ULP_FP64 is ONE tier weaker than BITEXACT, so the constraint
    // fails.  Adjacent-gap rejection: even the "almost-bitexact" tier
    // is rejected at the strict bitexact site.
    int result = bitexact_fence_consumer(std::move(fp64_value));
    return result;
}
