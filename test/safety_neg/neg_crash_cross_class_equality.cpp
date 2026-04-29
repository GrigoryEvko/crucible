// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing Crash<CLASS_A, T> with Crash<CLASS_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Class, T) instantiation has its OWN
// friend taking two Crash<Class, T>&.  Cross-class comparison
// fails to find a viable operator==.
//
// Pins the type-level discipline that values from functions with
// DIFFERENT failure-mode contracts must not be compared as if
// they were the same kind of value.  A NoThrow result and an
// Abort-classified result may have identical bytes but mean
// fundamentally different things at the dispatcher's recovery-
// admission boundary; equating them silently would hide that.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/Crash.h>

using namespace crucible::safety;

int main() {
    Crash<CrashClass_v::NoThrow, int> nothrow_value{42};
    Crash<CrashClass_v::Abort,   int> abort_value{42};

    // Should FAIL: operator== for Crash<NoThrow, int> takes two
    // Crash<NoThrow, int>&; abort_value is Crash<Abort, int>.
    return static_cast<int>(nothrow_value == abort_value);
}
