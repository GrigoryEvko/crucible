// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hyparview takes the Init context.  A Test context is a different
// context, and it does not convert to Init.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4>(::foundation::effects::testing::test());
    return static_cast<int>(membership.active_size().value());
}
