// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_swim_membership takes the init context.  A test context must not
// build production membership state.

#include <crucible/canopy/Swim.h>

int main() {
    auto membership = crucible::canopy::mint_swim_membership(::foundation::effects::testing::test());
    (void)membership;
    return 0;
}
