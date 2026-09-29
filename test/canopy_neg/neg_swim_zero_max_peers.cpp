// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The peer table of a SWIM membership is a slot table, and a slot table
// holds at least one slot.  The shape of the mint refuses a zero bound.
#include <crucible/canopy/Swim.h>
int main() {
    auto membership = crucible::canopy::mint_swim_membership<0>(::foundation::effects::testing::init());
    (void)membership;
    return 0;
}
