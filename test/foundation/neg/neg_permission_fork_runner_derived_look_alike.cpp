// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to reach the fork body by deriving
// from PermissionForkRunner, so that it could re-declare the body public
// with a using-declaration or call it from a member.  The runner is
// final, so the derivation itself is refused, before any member is
// named.  A using-declaration of the body needs such a derived class, so
// the same refusal closes that route too.  Without final, the
// using-declaration would still meet the access check, but the class
// would be a place to hang further attempts, so the door is shut at the
// first step.
//
// Expected diagnostic: a final base cannot be derived from.

#include <foundation/permissions/PermissionFork.h>

namespace {
struct LookAlike : ::foundation::permissions::PermissionForkRunner {};
}  // namespace

int main() {
    return 0;
}
