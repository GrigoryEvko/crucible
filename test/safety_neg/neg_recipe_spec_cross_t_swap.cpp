// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing RecipeSpec<T_A> with RecipeSpec<T_B>
// when T_A != T_B.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/RecipeSpec.h>

#include <utility>

using namespace crucible::safety;

int main() {
    RecipeSpec<int>    int_value{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpec<double> dbl_value{3.14, Tolerance::ULP_FP16, RecipeFamily::Kahan};

    int_value.swap(dbl_value);

    using std::swap;
    swap(int_value, dbl_value);

    return 0;
}
