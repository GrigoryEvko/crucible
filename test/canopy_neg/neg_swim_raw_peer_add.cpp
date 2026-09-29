// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A peer must come through admit_swim_peer, which tags it as a SWIM
// member, before it can enter the membership view.  A raw identity does
// not convert.
#include <crucible/canopy/Swim.h>
int main() {
    auto membership = crucible::canopy::mint_swim_membership<4>(::foundation::effects::testing::init());
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    (void)membership.add_peer(peer);
    return 0;
}
