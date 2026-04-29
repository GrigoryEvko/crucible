// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing Crash<CLASS_A, T> with Crash<CLASS_B, T>
// when CLASS_A != CLASS_B.
//
// swap() takes a reference to the SAME class — a member taking
// `Crash<Class, T>&`.  Cross-class swap is rejected at overload
// resolution.  ALSO covers the namespace-level free `swap` because
// it's a friend declared inside the class template (each
// instantiation has its own friend; cross-instantiation arguments
// have no viable overload).
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/Crash.h>
#include <utility>

using namespace crucible::safety;

int main() {
    Crash<CrashClass_v::NoThrow, int> nothrow_value{42};
    Crash<CrashClass_v::Abort,   int> abort_value{7};

    // Should FAIL: Crash<NoThrow, int>::swap takes
    // Crash<NoThrow, int>&; abort_value is a different type.
    nothrow_value.swap(abort_value);

    using std::swap;
    swap(nothrow_value, abort_value);

    return nothrow_value.peek();
}
