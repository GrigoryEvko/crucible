// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_scuttlebutt takes the Init context.  A background context holds
// more capabilities than Init, but it is a different context, and it does
// not convert to Init.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = crucible::canopy::mint_scuttlebutt<4, 4>(::foundation::effects::testing::bg(),
                                                         crucible::canopy::admit_swim_peer(peer));
    (void)sync;
    return 0;
}
