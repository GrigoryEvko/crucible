// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A membership with no active slot has no overlay.  The shape of the mint
// refuses a zero active capacity.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<0, 8>(::foundation::effects::testing::init());
    return static_cast<int>(membership.active_size().value());
}
