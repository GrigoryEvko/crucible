// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-115 fixture #1: Scuttlebutt peer/key matrices are statically
// bounded and the peer bound must be non-zero.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    crucible::canopy::ScuttlebuttSync<0, 4>* sync = nullptr;
    (void)sync;
    return 0;
}
