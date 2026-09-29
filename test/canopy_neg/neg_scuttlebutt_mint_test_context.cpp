// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_scuttlebutt builds the anti-entropy state of a process, so it takes
// the Init context.  A Test context does not convert to it.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = crucible::canopy::mint_scuttlebutt<4, 4>(::foundation::effects::testing::test(),
                                                         crucible::canopy::admit_swim_peer(peer));
    (void)sync;
    return 0;
}
