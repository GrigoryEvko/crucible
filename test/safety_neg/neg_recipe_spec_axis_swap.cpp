// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing RecipeFamily where Tolerance is expected
// (or vice versa) at the RecipeSpec constructor.
//
// THE LOAD-BEARING REJECTION FOR THE PRODUCT-WRAPPER AXIS DISCIPLINE.
// RecipeSpec's constructor is `RecipeSpec(T, Tolerance, RecipeFamily)`.
// If a maintainer flipped the axes —
//   RecipeSpec(value, family, tier)   // ← flipped
// — the compile error catches the mistake even though both axes
// are uint8_t-backed strong scoped enums.
//
// [GCC-WRAPPER-TEXT] — constructor parameter-type mismatch.

#include <crucible/safety/RecipeSpec.h>

using namespace crucible::safety;

int main() {
    Tolerance    tier{Tolerance::ULP_FP16};
    RecipeFamily fam{RecipeFamily::Kahan};

    // Should FAIL: RecipeSpec<int>(int, Tolerance, RecipeFamily)
    // cannot accept (int, RecipeFamily, Tolerance) — axes flipped.
    RecipeSpec<int> bad{42, fam, tier};
    return bad.peek();
}
