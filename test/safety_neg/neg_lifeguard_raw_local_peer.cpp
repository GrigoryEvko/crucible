// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-213 fixture #2: Lifeguard's local identity must already be a
// SWIM-admitted peer. Raw CogIdentity cannot mint a membership substrate.

#include <crucible/canopy/Lifeguard.h>

int main() {
    crucible::cog::CogIdentity local{};
    local.uuid = crucible::cog::Uuid{1, 2};
    auto lifeguard = crucible::canopy::mint_lifeguard_swim<4>(
        crucible::effects::Init{},
        local);
    (void)lifeguard;
    return 0;
}
