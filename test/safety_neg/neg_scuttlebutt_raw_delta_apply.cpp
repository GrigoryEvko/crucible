// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// apply_delta merges a delta that came from a peer, so it takes a gossiped
// delta.  A raw delta cannot be merged into local CRDT state.

#include <crucible/canopy/Scuttlebutt.h>

#include <cstdint>

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = cc::mint_scuttlebutt<4, 4>(::foundation::effects::testing::init(), cc::admit_swim_peer(peer));
    cc::GSet<std::uint64_t, 4> set{};
    cc::ScuttlebuttDelta<cc::GSet<std::uint64_t, 4>::state_type> delta{};
    (void)sync.apply_delta(delta, set);
    return 0;
}
