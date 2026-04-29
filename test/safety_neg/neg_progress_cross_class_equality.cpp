// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing Progress<CLASS_A, T> with Progress<CLASS_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Class, T) instantiation has its OWN
// friend taking two Progress<Class, T>&.  Cross-class comparison
// fails to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/Progress.h>

using namespace crucible::safety;

int main() {
    Progress<ProgressClass_v::Bounded,    int> bounded_value{42};
    Progress<ProgressClass_v::MayDiverge, int> diverge_value{42};

    // Should FAIL: operator== for Progress<Bounded, int> takes two
    // Progress<Bounded, int>&; diverge_value is Progress<MayDiverge,
    // int>.
    return static_cast<int>(bounded_value == diverge_value);
}
