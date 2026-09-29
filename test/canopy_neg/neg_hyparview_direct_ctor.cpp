// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of HyParViewMembership is private.  mint_hyparview is
// the only door, so a membership cannot come into being without the Init
// context.
#include <crucible/canopy/HyParView.h>

#include <span>

int main() {
    namespace cc = crucible::canopy;
    cc::HyParViewMembership<4, 8> membership{std::span<const cc::HyParViewPeer>{}, std::span<const cc::HyParViewPeer>{},
                                             cc::HyParViewConfig{}};
    (void)membership;
    return 0;
}
