#include <crucible/canopy/Plumtree.h>

// Provokes the HyParView peer gate: raw CogIdentity cannot mutate Plumtree
// link state without first being admitted by the HyParView overlay.
int main() {
    crucible::canopy::PlumtreeBroadcast<4, 8> broadcast{};
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto result = broadcast.add_eager_peer(raw);
    return result.has_value() ? 0 : 1;
}
