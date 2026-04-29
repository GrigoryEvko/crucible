// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing Progress<CLASS_A, T> with Progress<CLASS_B, T>
// when CLASS_A != CLASS_B.
//
// swap() takes a reference to the SAME class — a member taking
// `Progress<Class, T>&`.  Cross-class swap is rejected at overload
// resolution.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/Progress.h>
#include <utility>

using namespace crucible::safety;

int main() {
    Progress<ProgressClass_v::Bounded,    int> bounded_value{42};
    Progress<ProgressClass_v::MayDiverge, int> diverge_value{7};

    // Should FAIL: Progress<Bounded, int>::swap takes
    // Progress<Bounded, int>&; diverge_value is a different type.
    bounded_value.swap(diverge_value);

    using std::swap;
    swap(bounded_value, diverge_value);

    return bounded_value.peek();
}
