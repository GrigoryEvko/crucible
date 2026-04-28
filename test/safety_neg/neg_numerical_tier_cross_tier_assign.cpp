// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning a NumericalTier<TIER_A, T> to a
// NumericalTier<TIER_B, T> when TIER_A != TIER_B.
//
// Different T_at template arguments produce DIFFERENT class
// instantiations.  There is no converting assignment operator and
// no implicit conversion between them — the type system enforces
// per-tier disjointness.  This is the load-bearing structural
// property: the call site that NEEDS BITEXACT cannot accidentally
// receive ULP_FP16.
//
// Concrete bug-class this catches: a refactor that added a
// templated converting-assign operator on NumericalTier (e.g. for
// "convenience" of cross-tier copy) would let a Forge Phase E
// RecipeSelect output rowed at ULP_FP16 silently flow into a
// BITEXACT_STRICT consumer's slot, producing a recipe-tier
// downgrade visible only at the cross-vendor numerics CI 12 hours
// later.  The neg-compile pins the per-tier identity at the
// assignment surface.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/NumericalTier.h>

using namespace crucible::safety;

int main() {
    NumericalTier<Tolerance::BITEXACT, int>  bx{42};
    NumericalTier<Tolerance::ULP_FP16, int>  fp16{7};

    // Should FAIL: bx and fp16 are DIFFERENT types — different
    // template instantiations of NumericalTier.  No converting
    // assignment exists; the assignment operator's left and right
    // sides must match exactly.
    bx = fp16;
    return bx.peek();
}
