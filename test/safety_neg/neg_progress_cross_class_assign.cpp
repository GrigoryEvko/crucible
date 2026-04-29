// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning a Progress<CLASS_A, T> to a
// Progress<CLASS_B, T> when CLASS_A != CLASS_B.
//
// Different Class template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion.
//
// Concrete bug-class this catches: a refactor adding a templated
// converting-assign operator on Progress would let a MayDiverge-
// tier value silently flow into a Bounded-tier slot.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/Progress.h>

using namespace crucible::safety;

int main() {
    Progress<ProgressClass_v::Bounded,    int> bounded_value{42};
    Progress<ProgressClass_v::MayDiverge, int> diverge_value{7};

    // Should FAIL: bounded_value and diverge_value are DIFFERENT types.
    bounded_value = diverge_value;
    return bounded_value.peek();
}
