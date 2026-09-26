// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_lifeguard_swim takes the Init context.  A background context is a
// different context, and it does not convert to Init.
#include <crucible/canopy/Lifeguard.h>
int main() {
    crucible::cog::CogIdentity local{};
    local.uuid = crucible::cog::Uuid{1, 2};
    auto lifeguard = crucible::canopy::mint_lifeguard_swim<4>(::foundation::effects::testing::bg(),
                                                              crucible::canopy::admit_swim_peer(local));
    (void)lifeguard;
    return 0;
}
