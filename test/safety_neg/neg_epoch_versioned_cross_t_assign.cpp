// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning EpochVersioned<T_A> to EpochVersioned<T_B>
// when T_A != T_B.
//
// Different value-type template arguments produce DIFFERENT class
// instantiations.  Same fence as Budgeted's cross_t_assign — pinned
// independently for the second product wrapper because each
// instantiation has its own assignment operator and silent
// conversion would mask cross-T contamination at production
// Canopy collective sites.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/EpochVersioned.h>

using namespace crucible::safety;

int main() {
    EpochVersioned<int>    int_value{42, Epoch{1}, Generation{1}};
    EpochVersioned<double> dbl_value{3.14, Epoch{2}, Generation{2}};

    // Should FAIL: different types, no implicit conversion.
    int_value = dbl_value;
    return 0;
}
