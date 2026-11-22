// Negative-compile test. This file must fail to compile.
//
// The base has no CRUCIBLE_DEBUG_ASSERT.  NDEBUG removed its check, so a
// Release library kept no check.  A precondition is CRUCIBLE_PRE, and a
// check in the middle of a body is CRUCIBLE_FATAL_INVARIANT.  The two
// check in each build.

#include <foundation/Platform.h>

int main() {
    int const value = 1;
    CRUCIBLE_DEBUG_ASSERT(value == 1);
    return 0;
}
