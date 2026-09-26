// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_swim_membership takes the Init context.  A background context is a
// different context, and it does not convert to Init.
#include <crucible/canopy/Swim.h>
int main() {
    auto membership = crucible::canopy::mint_swim_membership<4>(::foundation::effects::testing::bg());
    (void)membership;
    return 0;
}
