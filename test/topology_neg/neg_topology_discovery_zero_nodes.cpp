// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A discovery snapshot needs at least one node slot and one edge slot.  A
// snapshot with no node slot could hold nothing, so its shape is refused.

#include <crucible/topology/Discovery.h>

int main() {
    ::fixy::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = crucible::topology::mint_discovery_snapshot<0, 1>(ctx);
    (void)snapshot;
    return 0;
}
