// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-114 fixture #1: SWIM membership storage is statically bounded
// and the bound must be non-zero.

#include <crucible/canopy/Swim.h>

int main() {
    crucible::canopy::SwimMembership<0> membership;
    (void)membership;
    return 0;
}
