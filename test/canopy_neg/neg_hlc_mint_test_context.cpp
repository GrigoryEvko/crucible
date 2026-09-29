// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hlc takes the init context.  A test context must not build a
// production clock.

#include <crucible/canopy/Hlc.h>

int main() {
    auto clock = crucible::canopy::mint_hlc(::foundation::effects::testing::test());
    (void)clock;
    return 0;
}
