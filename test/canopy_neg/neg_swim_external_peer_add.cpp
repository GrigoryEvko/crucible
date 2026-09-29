// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An External CogIdentity cannot pass as a SwimMember.  Discovery and
// transport input must take the SWIM admission door explicitly.
#include <crucible/canopy/Swim.h>
int main() {
    auto membership = crucible::canopy::mint_swim_membership<4>(::foundation::effects::testing::init());
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto external = ::fixy::mint_tagged<::fixy::tags::source::External>(peer);
    (void)membership.add_peer(external);
    return 0;
}
