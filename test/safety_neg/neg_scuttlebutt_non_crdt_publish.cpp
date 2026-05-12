// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-115 fixture #4: publish_local_change is constrained to CRDT
// carriers with state() plus gossiped-state merge support.

#include <crucible/canopy/Scuttlebutt.h>

struct NotCrdt {};

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = cc::mint_scuttlebutt<4, 4>(
        crucible::effects::Init{},
        cc::admit_swim_peer(peer));
    auto key = cc::admit_scuttlebutt_key("bad").value();
    NotCrdt value{};
    (void)sync.publish_local_change(key, value);
    return 0;
}
