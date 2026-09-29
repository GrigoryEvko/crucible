// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// publish_local_change accepts only a state-based CRDT: a value with
// state(), a merge of a gossiped state, and a copyable state.

#include <crucible/canopy/Scuttlebutt.h>

struct NotCrdt {};

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = cc::mint_scuttlebutt<4, 4>(::foundation::effects::testing::init(), cc::admit_swim_peer(peer));
    auto key = cc::admit_scuttlebutt_key("bad").value();
    NotCrdt value{};
    (void)sync.publish_local_change(key, value);
    return 0;
}
