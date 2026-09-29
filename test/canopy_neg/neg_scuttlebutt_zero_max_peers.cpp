// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The peer and key tables of a sync have static bounds, and the peer bound
// must not be zero.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    crucible::canopy::ScuttlebuttSync<0, 4>* sync = nullptr;
    (void)sync;
    return 0;
}
