#include <crucible/canopy/HyParView.h>

// Provokes the HyParViewShape concept: an active view with zero capacity cannot
// form a usable overlay.
int main() {
    auto membership = crucible::canopy::mint_hyparview<0, 8>(
        crucible::effects::testing::init());
    return static_cast<int>(membership.active_size().value());
}
