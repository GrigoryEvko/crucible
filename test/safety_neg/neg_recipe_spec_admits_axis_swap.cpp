// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing RecipeFamily where Tolerance is expected at
// the RecipeSpec<T>::admits(Tolerance, RecipeFamily) admission gate.
//
// COMPANION TO neg_recipe_spec_axis_swap (which fences the
// constructor signature).  admits() is the production
// admission-gate signature at Forge Phase E.RecipeSelect dispatch
// sites.  A flipped-axis call is a compile error.
//
// [GCC-WRAPPER-TEXT] — admits parameter-type rejection.

#include <crucible/safety/RecipeSpec.h>

using namespace crucible::safety;

int main() {
    RecipeSpec<int> kernel{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};

    Tolerance    req_tier{Tolerance::ULP_FP8};
    RecipeFamily req_family{RecipeFamily::Kahan};

    // Should FAIL: admits(Tolerance, RecipeFamily) requires axes
    // in declared order; passing (RecipeFamily, Tolerance) is a
    // type mismatch.
    return static_cast<int>(
        kernel.admits(req_family, req_tier));
}
