// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-114 fixture #3: an External CogIdentity cannot masquerade as
// a SwimMember.  Discovery / transport input must choose the SWIM
// admission lane explicitly.

#include <crucible/canopy/Swim.h>

int main() {
    crucible::canopy::SwimMembership<4> membership;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    crucible::safety::Tagged<
        crucible::cog::CogIdentity,
        crucible::safety::source::External> external{peer};
    (void)membership.add_peer(external);
    return 0;
}
