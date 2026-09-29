// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hlc takes the init context.  A background context does not
// convert to it, so a background thread cannot mint the clock.

#include <crucible/canopy/Hlc.h>

int main() {
    auto clock = crucible::canopy::mint_hlc(::foundation::effects::testing::bg());
    (void)clock;
    return 0;
}
