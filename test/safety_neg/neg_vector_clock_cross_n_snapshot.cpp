// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-215 fixture #3: snapshots for different MaxNodes values are
// structurally distinct and must not cross a fixed-size API boundary.

#include <crucible/canopy/VectorClock.h>

void wants_four(crucible::canopy::VectorClockSnapshot<4>);

int main() {
    crucible::canopy::VectorClockSnapshot<3> three{};
    wants_four(three);
    return 0;
}
