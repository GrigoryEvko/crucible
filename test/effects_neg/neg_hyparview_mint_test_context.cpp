#include <crucible/canopy/HyParView.h>

// Provokes mint_hyparview's Init-only requires clause: Test contexts cannot
// mint runtime membership state.
int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(
        crucible::effects::Test{});
    return static_cast<int>(membership.active_size().value());
}
