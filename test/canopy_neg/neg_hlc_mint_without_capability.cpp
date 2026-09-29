// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hlc has no overload without the startup capability, so a call
// with no argument finds no function to call.

#include <crucible/canopy/Hlc.h>

int main() {
    auto clock = crucible::canopy::mint_hlc();
    (void)clock;
    return 0;
}
