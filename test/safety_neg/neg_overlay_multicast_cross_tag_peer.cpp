#include <crucible/cntp/OverlayMulticast.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    crucible::cntp::OverlayPeerRef raw{.uuid = crucible::cog::Uuid{1, 1}};
    auto external = ::fixy::mint_tagged<::fixy::tags::source::External>(raw);
    auto plan = crucible::cntp::mint_overlay_multicast<4, 4, 2>(init, external);
    return static_cast<int>(plan.peer_count());
}
