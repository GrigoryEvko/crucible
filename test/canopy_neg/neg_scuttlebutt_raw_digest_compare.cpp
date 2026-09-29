// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// compare_digest reads a digest that came from a peer, so it takes a
// gossiped digest.  A raw digest does not convert to one.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = cc::mint_scuttlebutt<4, 4>(::foundation::effects::testing::init(), cc::admit_swim_peer(peer));
    cc::ScuttlebuttDigest<4, 4> digest{};
    (void)sync.compare_digest(digest);
    return 0;
}
