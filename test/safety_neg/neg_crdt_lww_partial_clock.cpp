// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-214 fixture #4: LWW registers need a total-order clock.  A
// vector-clock snapshot is partially ordered and can report unordered
// concurrent events, so it is rejected at the type boundary.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::LwwRegister<
        int,
        crucible::canopy::VectorClockSnapshot<4>> reg;
    (void)reg;
    return 0;
}
