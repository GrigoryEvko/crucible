// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_plumtree takes the Init context.  A Test context is a different
// context, and it does not convert to Init.
#include <crucible/canopy/Plumtree.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    auto broadcast = crucible::canopy::mint_plumtree<4, 8>(::foundation::effects::testing::test(), membership);
    return static_cast<int>(broadcast.link_count().value());
}
