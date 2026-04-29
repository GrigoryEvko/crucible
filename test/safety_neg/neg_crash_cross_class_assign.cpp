// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning Crash<CLASS_A, T> to Crash<CLASS_B, T>
// when CLASS_A != CLASS_B.
//
// Different Class template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/Crash.h>

using namespace crucible::safety;

int main() {
    Crash<CrashClass_v::NoThrow, int> nothrow_value{42};
    Crash<CrashClass_v::Abort,   int> abort_value{7};

    // Should FAIL: nothrow_value and abort_value are DIFFERENT types.
    nothrow_value = abort_value;
    return nothrow_value.peek();
}
