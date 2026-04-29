// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning RecipeSpec<T_A> to RecipeSpec<T_B> when T_A != T_B.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/RecipeSpec.h>

using namespace crucible::safety;

int main() {
    RecipeSpec<int>    int_value{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpec<double> dbl_value{3.14, Tolerance::ULP_FP16, RecipeFamily::Kahan};

    int_value = dbl_value;
    return 0;
}
