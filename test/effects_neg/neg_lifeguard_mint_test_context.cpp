// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-213 fixture #3: Lifeguard state is minted at Init authority.
// Test-only contexts cannot construct production membership state.

#include <crucible/canopy/Lifeguard.h>

int main() {
    auto local = crucible::canopy::admit_swim_peer({});
    auto lifeguard = crucible::canopy::mint_lifeguard_swim<4>(
        crucible::effects::testing::test(),
        local);
    (void)lifeguard;
    return 0;
}
