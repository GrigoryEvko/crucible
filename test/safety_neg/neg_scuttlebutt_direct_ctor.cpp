// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of ScuttlebuttSync is private.  mint_scuttlebutt is the
// only door, so a sync cannot come into being without the Init context.

#include <crucible/canopy/Scuttlebutt.h>

#include <span>

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    cc::ScuttlebuttSync<4, 4> sync{cc::admit_swim_peer(peer), std::span<const cc::SwimPeer>{},
                                   cc::ScuttlebuttConfig{}};
    (void)sync;
    return 0;
}
