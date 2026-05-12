// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-115 fixture #3: apply_delta accepts source::Gossiped deltas.
// A raw delta cannot be merged into local CRDT state.

#include <crucible/canopy/Scuttlebutt.h>

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = cc::mint_scuttlebutt<4, 4>(
        crucible::effects::Init{},
        cc::admit_swim_peer(peer));
    cc::GSet<std::uint64_t, 4> set{};
    cc::ScuttlebuttDelta<cc::GSet<std::uint64_t, 4>::state_type> delta{};
    (void)sync.apply_delta(delta, set);
    return 0;
}
