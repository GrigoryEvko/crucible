// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Lifeguard state is minted under init authority.  A test context must
// not build production membership state.

#include <crucible/canopy/Lifeguard.h>

int main() {
    auto local = crucible::canopy::admit_swim_peer({});
    auto lifeguard = crucible::canopy::mint_lifeguard_swim<4>(::foundation::effects::testing::test(), local);
    (void)lifeguard;
    return 0;
}
