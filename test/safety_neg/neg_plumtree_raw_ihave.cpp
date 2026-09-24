#include <crucible/canopy/Plumtree.h>

// Provokes the IHAVE provenance gate: repair summaries must cross the
// gossiped boundary before they can request missing messages.
int main() {
    crucible::canopy::PlumtreeBroadcast<4, 8> broadcast{};
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto peer = crucible::canopy::admit_hyparview_peer(raw).value();
    (void)broadcast.add_lazy_peer(peer);
    crucible::canopy::PlumtreeIHave<8> ihave{};
    auto result = broadcast.receive_ihave(peer, ihave);
    return result.has_value() ? 0 : 1;
}
