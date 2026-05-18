#include <crucible/canopy/Plumtree.h>

// Provokes mint_plumtree's Init-only gate: Test contexts cannot mint runtime
// broadcast state from a HyParView membership.
int main() {
    crucible::canopy::HyParViewMembership<4, 8> membership{};
    auto broadcast = crucible::canopy::mint_plumtree<4, 8>(
        crucible::effects::testing::test(),
        membership);
    return static_cast<int>(broadcast.link_count().value());
}
