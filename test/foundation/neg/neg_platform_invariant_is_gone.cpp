// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The base has no CRUCIBLE_INVARIANT.  Under NDEBUG it was an [[assume]],
// so a false condition was undefined behavior in a Release library.  A
// check in the middle of a body is CRUCIBLE_FATAL_INVARIANT, which checks
// in each build and gives the optimizer no assumption.

#include <foundation/Platform.h>

int main() {
    int const value = 1;
    CRUCIBLE_INVARIANT(value == 1);
    return 0;
}
