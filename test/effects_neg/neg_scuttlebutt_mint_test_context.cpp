// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-115 fixture #5: mint_scuttlebutt is an Init-context factory.
// Test cannot fabricate production anti-entropy state.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = crucible::canopy::mint_scuttlebutt<4, 4>(
        crucible::effects::Test{},
        crucible::canopy::admit_swim_peer(peer));
    (void)sync;
    return 0;
}
