// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hyparview takes the Init context.  A background context is a
// different context, and it does not convert to Init.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4>(::foundation::effects::testing::bg());
    return static_cast<int>(membership.active_size().value());
}
