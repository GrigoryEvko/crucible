// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing NumaPlacement<T_A> with NumaPlacement<T_B>
// when T_A != T_B.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/NumaPlacement.h>

#include <utility>

using namespace crucible::safety;

int main() {
    NumaPlacement<int>    int_value{42, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacement<double> dbl_value{3.14, NumaNodeId{2}, AffinityMask{0b11}};

    int_value.swap(dbl_value);

    using std::swap;
    swap(int_value, dbl_value);

    return 0;
}
