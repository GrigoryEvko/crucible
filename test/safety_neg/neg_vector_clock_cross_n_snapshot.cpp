// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Snapshots for different MaxNodes values are distinct types, so a
// snapshot must not cross an API boundary sized for another count.

#include <crucible/canopy/VectorClock.h>

void wants_four(crucible::canopy::VectorClockSnapshot<4>);

int main() {
    crucible::canopy::VectorClockSnapshot<3> three{};
    wants_four(three);
    return 0;
}
