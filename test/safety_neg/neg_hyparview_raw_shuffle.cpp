#include <crucible/canopy/HyParView.h>

// Provokes the shuffle provenance gate: passive-view samples must be tagged as
// gossiped before they can update local HyParView state.
int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(
        crucible::effects::Init{});
    crucible::canopy::HyParViewShuffle<8> raw{};
    auto result = membership.apply_shuffle(raw);
    return result.has_value() ? 0 : 1;
}
