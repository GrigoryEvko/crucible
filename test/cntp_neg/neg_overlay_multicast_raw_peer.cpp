#include <crucible/cntp/OverlayMulticast.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    crucible::cntp::OverlayPeerRef raw{.uuid = crucible::cog::Uuid{1, 1}};
    auto plan = crucible::cntp::mint_overlay_multicast<4, 4, 2>(init, raw);
    return static_cast<int>(plan.peer_count());
}
