// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-114 fixture #4: mint_swim_membership is an Init-context mint.
// A Test context cannot fabricate production membership state.

#include <crucible/canopy/Swim.h>

int main() {
    auto membership =
        crucible::canopy::mint_swim_membership(crucible::effects::Test{});
    (void)membership;
    return 0;
}
