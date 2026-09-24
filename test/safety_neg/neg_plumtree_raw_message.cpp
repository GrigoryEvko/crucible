#include <crucible/canopy/Plumtree.h>

// Provokes the message provenance gate: received messages must be tagged as
// gossiped before they update duplicate/history/link state.
int main() {
    crucible::canopy::PlumtreeBroadcast<4, 8> broadcast{};
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto peer = crucible::canopy::admit_hyparview_peer(raw).value();
    (void)broadcast.add_eager_peer(peer);
    crucible::canopy::PlumtreeMessage message{};
    auto result = broadcast.receive_message(peer, message);
    return result.has_value() ? 0 : 1;
}
