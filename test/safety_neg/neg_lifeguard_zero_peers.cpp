// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-213 fixture #1: Lifeguard peer storage is statically bounded
// and MaxPeers=0 cannot carry SWIM/LHM state.

#include <crucible/canopy/Lifeguard.h>

int main() {
    crucible::canopy::LifeguardSwim<0, 8, 4, 8> lifeguard{
        crucible::canopy::admit_swim_peer({})};
    (void)lifeguard;
    return 0;
}
