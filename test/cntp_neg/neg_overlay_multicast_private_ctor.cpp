#include <crucible/cntp/OverlayMulticast.h>

// The plan constructor is private.  Only mint_overlay_multicast, which
// requires an Init context, builds a plan.
int main() {
    auto peer = crucible::cntp::admit_overlay_peer(crucible::cog::CogIdentity{.uuid = crucible::cog::Uuid{1, 1}});
    crucible::cntp::OverlayMulticastPlan<4, 4, 2> plan{peer.value(), {}, crucible::cntp::OverlayMulticastConfig{}};
    return static_cast<int>(plan.peer_count());
}
