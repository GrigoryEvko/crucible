// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling combine_max() with RecipeSpec<U> argument
// when receiver is RecipeSpec<T> and T != U.
//
// [GCC-WRAPPER-TEXT] — combine_max parameter-type rejection.

#include <crucible/safety/RecipeSpec.h>

using namespace crucible::safety;

int main() {
    RecipeSpec<int>    int_value{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpec<double> dbl_value{3.14, Tolerance::ULP_FP16, RecipeFamily::Kahan};

    auto bad = int_value.combine_max(dbl_value);
    return bad.peek();
}
