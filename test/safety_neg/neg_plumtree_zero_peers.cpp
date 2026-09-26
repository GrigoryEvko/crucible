// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A broadcast tree with no link slot reaches no peer.  The shape gate of
// the mint refuses a zero peer capacity.
#include <crucible/canopy/Plumtree.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    auto broadcast = crucible::canopy::mint_plumtree<0, 8>(::foundation::effects::testing::init(), membership);
    return static_cast<int>(broadcast.link_count().value());
}
