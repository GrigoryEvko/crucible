// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The local identity of a Lifeguard must already be a SWIM-admitted
// peer.  A raw CogIdentity cannot mint a membership substrate.

#include <crucible/canopy/Lifeguard.h>

int main() {
    crucible::cog::CogIdentity local{};
    local.uuid = crucible::cog::Uuid{1, 2};
    auto lifeguard = crucible::canopy::mint_lifeguard_swim<4>(::foundation::effects::testing::init(), local);
    (void)lifeguard;
    return 0;
}
