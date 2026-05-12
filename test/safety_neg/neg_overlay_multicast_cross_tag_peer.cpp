#include <crucible/cntp/OverlayMulticast.h>

int main() {
    crucible::effects::ColdInitCtx init{};
    crucible::cntp::OverlayPeerRef raw{.uuid = crucible::cog::Uuid{1, 1}};
    crucible::safety::Tagged<crucible::cntp::OverlayPeerRef,
                             crucible::safety::source::External>
        external{raw};
    auto plan = crucible::cntp::mint_overlay_multicast<4, 4, 2>(
        init, external);
    return static_cast<int>(plan.peer_count());
}
