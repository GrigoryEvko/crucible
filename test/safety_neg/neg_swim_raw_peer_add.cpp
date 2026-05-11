// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-114 fixture #2: peers must be admitted as source::SwimMember
// before they can enter the membership view.

#include <crucible/canopy/Swim.h>

int main() {
    crucible::canopy::SwimMembership<4> membership;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    (void)membership.add_peer(peer);
    return 0;
}
