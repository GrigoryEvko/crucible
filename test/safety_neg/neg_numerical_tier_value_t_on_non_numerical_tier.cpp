// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D21 fixture — pins the constrained-extractor discipline on
// IsNumericalTier.h.  numerical_tier_value_t is constrained on
// `requires is_numerical_tier_v<T>`; instantiating it on a non-
// NumericalTier type must fail at the requires clause itself, NOT
// silently yield `void`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsNumericalTier.h>

int main() {
    // bare int is not a NumericalTier specialisation.
    using V = crucible::safety::extract::numerical_tier_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
