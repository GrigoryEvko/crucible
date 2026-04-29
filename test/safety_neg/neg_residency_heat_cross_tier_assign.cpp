// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning ResidencyHeat<TIER_A, T> to
// ResidencyHeat<TIER_B, T> when TIER_A != TIER_B.
//
// Different Tier template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion.
//
// Concrete bug-class this catches: a refactor adding a templated
// converting-assign operator on ResidencyHeat would let a Cold-tier
// value silently flow into a Hot-tier slot — defeating the
// CRUCIBLE.md §L2 KernelCache working-set discipline at the
// assignment boundary.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/ResidencyHeat.h>

using namespace crucible::safety;

int main() {
    ResidencyHeat<ResidencyHeatTag_v::Hot,  int> hot_value{42};
    ResidencyHeat<ResidencyHeatTag_v::Cold, int> cold_value{7};

    // Should FAIL: hot_value and cold_value are DIFFERENT types.
    hot_value = cold_value;
    return hot_value.peek();
}
