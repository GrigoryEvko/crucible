// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning Budgeted<T_A> to Budgeted<T_B>
// when T_A != T_B.
//
// Different value-type template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion.
//
// THE FIRST PRODUCT-WRAPPER NEG-FIXTURE — pins that even though
// Budgeted's lattice substrate is shared (ProductLattice<BitsBudget,
// PeakBytes>), distinct T types remain structurally disjoint.  A
// refactor that introduced an implicit-conversion path between
// Budgeted<int> and Budgeted<double> (say, via a misguided
// "convenience" templated assign operator) would defeat the
// type-fenced footprint-tracking discipline at every dispatch
// boundary.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/Budgeted.h>

using namespace crucible::safety;

int main() {
    Budgeted<int>    int_value{42, BitsBudget{1024}, PeakBytes{4096}};
    Budgeted<double> dbl_value{3.14, BitsBudget{2048}, PeakBytes{8192}};

    // Should FAIL: int_value and dbl_value are DIFFERENT types.
    int_value = dbl_value;
    return 0;
}
