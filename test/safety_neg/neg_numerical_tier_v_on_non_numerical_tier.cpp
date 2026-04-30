// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D21 fixture — pins the constrained-extractor discipline.
// numerical_tier_v is constrained on `requires is_numerical_tier_v<T>`;
// instantiating it on a non-NumericalTier type must fail at the
// requires clause, NOT silently yield an undefined Tolerance value.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsNumericalTier.h>

int main() {
    auto t = crucible::safety::extract::numerical_tier_v<int>;
    (void)t;
    return 0;
}
