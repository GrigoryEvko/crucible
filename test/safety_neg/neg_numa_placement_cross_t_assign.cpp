// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning NumaPlacement<T_A> to NumaPlacement<T_B>
// when T_A != T_B.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/NumaPlacement.h>

using namespace crucible::safety;

int main() {
    NumaPlacement<int>    int_value{42, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacement<double> dbl_value{3.14, NumaNodeId{2}, AffinityMask{0b11}};

    // Should FAIL: different types, no implicit conversion.
    int_value = dbl_value;
    return 0;
}
