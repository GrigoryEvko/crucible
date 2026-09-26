// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An LWW register needs a total-order clock.  A vector-clock snapshot is
// partially ordered and can report concurrent events as unordered, so it
// is refused at the type boundary.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::LwwRegister<int, crucible::canopy::VectorClockSnapshot<4>> reg;
    (void)reg;
    return 0;
}
