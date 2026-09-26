// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A failed active peer is replaced from the passive view, so the passive
// view is at least as large as the active view.  The shape of the mint
// refuses an active capacity above the passive capacity.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<8, 4>(::foundation::effects::testing::init());
    return static_cast<int>(membership.active_size().value());
}
