// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of LifeguardSwim is private.  mint_lifeguard_swim is the
// only door, so a Lifeguard cannot come into being without the Init
// context, even when the caller holds one.
#include <crucible/canopy/Lifeguard.h>

#include <span>

int main() {
    namespace cc = crucible::canopy;
    crucible::cog::CogIdentity local{};
    local.uuid = crucible::cog::Uuid{1, 2};
    cc::LifeguardSwim<4, 8, 4, 8> lifeguard{::foundation::effects::testing::init(), cc::admit_swim_peer(local),
                                            std::span<const cc::SwimPeer>{}, cc::LifeguardConfig{},
                                            cc::SwimConfig{}};
    (void)lifeguard;
    return 0;
}
