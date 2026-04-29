// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling combine_max() with NumaPlacement<U> argument
// when receiver is NumaPlacement<T> and T != U.
//
// [GCC-WRAPPER-TEXT] — combine_max parameter-type rejection.

#include <crucible/safety/NumaPlacement.h>

using namespace crucible::safety;

int main() {
    NumaPlacement<int>    int_value{42, NumaNodeId{2}, AffinityMask::single(0)};
    NumaPlacement<double> dbl_value{3.14, NumaNodeId{2}, AffinityMask::single(1)};

    auto bad = int_value.combine_max(dbl_value);
    return bad.peek();
}
