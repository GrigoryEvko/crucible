// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The peer table of a Lifeguard is a slot table, and a slot table holds
// at least one slot.  The shape of the mint refuses a zero bound.
#include <crucible/canopy/Lifeguard.h>
int main() {
    crucible::cog::CogIdentity local{};
    local.uuid = crucible::cog::Uuid{1, 2};
    auto lifeguard = crucible::canopy::mint_lifeguard_swim<0, 8, 4, 8>(::foundation::effects::testing::init(),
                                                                       crucible::canopy::admit_swim_peer(local));
    (void)lifeguard;
    return 0;
}
