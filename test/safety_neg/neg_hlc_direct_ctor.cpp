// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of Hlc is private, and mint_hlc is its only friend.  A
// clock built without the mint would read the wall clock with no init
// context behind it.

#include <crucible/canopy/Hlc.h>

int main() {
    crucible::canopy::Hlc clock;
    (void)clock.peek();
    return 0;
}
