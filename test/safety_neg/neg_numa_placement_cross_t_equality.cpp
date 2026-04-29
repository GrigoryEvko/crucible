// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing NumaPlacement<T_A> with NumaPlacement<T_B>
// via operator== when T_A != T_B.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/NumaPlacement.h>

using namespace crucible::safety;

int main() {
    NumaPlacement<int>    int_value{42, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacement<double> dbl_value{3.14, NumaNodeId{2}, AffinityMask{0b11}};

    return static_cast<int>(int_value == dbl_value);
}
