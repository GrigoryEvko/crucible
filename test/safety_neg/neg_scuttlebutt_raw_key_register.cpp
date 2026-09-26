// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// register_state takes a local key.  A raw key hash cannot pass around the
// key admission door.

#include <crucible/canopy/Scuttlebutt.h>

#include <cstdint>

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{1, 2};
    auto sync = cc::mint_scuttlebutt<4, 4>(::foundation::effects::testing::init(), cc::admit_swim_peer(peer));
    cc::GSet<std::uint64_t, 4> set{};
    cc::ScuttlebuttKey key{.hash = 1, .length = 1};
    (void)sync.register_state(key, set);
    return 0;
}
