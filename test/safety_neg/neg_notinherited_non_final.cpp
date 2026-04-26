// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `crucible::safety::assert_not_inherited<T>()` on
// a type `T` that is NOT marked `final`.  The consteval helper's
// static_assert fires with the framework-controlled named diagnostic
// `[NotInherited_Not_Final]`.
//
// This is the CALLER-SIDE WITNESS form of the FinalBy/NotInherited
// primitive: an API that demands a final type catches the caller who
// passed a non-final one at the call site, not deep in template
// expansion.
//
// Task #148 (A8-P3 FinalBy<T>/NotInherited<T>); see
// include/crucible/safety/NotInherited.h for the mechanism.

#include <crucible/safety/NotInherited.h>

struct NotFinal {};  // missing `final` keyword

int main() {
    // Expected diagnostic:
    //   static assertion failed: [NotInherited_Not_Final] ...
    crucible::safety::assert_not_inherited<NotFinal>();
    return 0;
}
