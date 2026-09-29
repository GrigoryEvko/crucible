// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A broadcast tree has a link slot for each peer that the active view of
// its overlay can hold.  An overlay of eight active slots does not fit a
// tree of four link slots, so the shape gate of the mint refuses it.
#include <crucible/canopy/Plumtree.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<8, 64>(::foundation::effects::testing::init());
    auto broadcast = crucible::canopy::mint_plumtree<4, 8>(::foundation::effects::testing::init(), membership);
    return static_cast<int>(broadcast.link_count().value());
}
