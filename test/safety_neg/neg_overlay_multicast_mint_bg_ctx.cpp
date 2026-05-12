#include <crucible/cntp/OverlayMulticast.h>

int main() {
    crucible::effects::BgDrainCtx bg{};
    auto peer = crucible::cntp::admit_overlay_peer(
        crucible::cog::CogIdentity{.uuid = crucible::cog::Uuid{1, 1}});
    auto plan = crucible::cntp::mint_overlay_multicast<4, 4, 2>(bg, *peer);
    return static_cast<int>(plan.peer_count());
}
