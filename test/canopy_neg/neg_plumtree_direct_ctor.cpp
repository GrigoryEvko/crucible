// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of PlumtreeBroadcast is private.  mint_plumtree is the
// only door, so a tree cannot come into being without the Init context
// and the shape gate of the mint.
#include <crucible/canopy/Plumtree.h>

int main() {
    namespace cc = crucible::canopy;
    auto membership = cc::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    cc::PlumtreeBroadcast<4, 8> broadcast{membership, cc::PlumtreeConfig{}};
    (void)broadcast;
    return 0;
}
