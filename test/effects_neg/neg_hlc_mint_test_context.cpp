// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-138 mint fixture: mint_hlc is an Init-only factory.  Test
// contexts cannot mint a production HLC instance.

#include <crucible/canopy/Hlc.h>

int main() {
    auto clock = crucible::canopy::mint_hlc(crucible::effects::Test{});
    (void)clock;
    return 0;
}
