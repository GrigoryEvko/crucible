#include <crucible/cntp/OverlayMulticast.h>

int main() {
    crucible::effects::ColdInitCtx init{};
    crucible::cntp::OverlayPeerRef raw{.uuid = crucible::cog::Uuid{1, 1}};
    auto plan = crucible::cntp::mint_overlay_multicast<4, 4, 2>(init, raw);
    return static_cast<int>(plan.peer_count());
}
