// Negative-compile test. This file must fail to compile.
//
// The base has no CRUCIBLE_ASSERT.  It was a raw contract_assert, which the
// ignore semantic removes with no assumption.  A precondition is
// CRUCIBLE_PRE, and a check in the middle of a body is
// CRUCIBLE_FATAL_INVARIANT.

#include <foundation/Platform.h>

int main() {
    int const value = 1;
    CRUCIBLE_ASSERT(value == 1);
    return 0;
}
