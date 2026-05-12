#include <crucible/canopy/HyParView.h>

// Provokes the HyParViewPeer provenance gate: source::External identities
// cannot substitute for peers admitted into the HyParView overlay.
int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(
        crucible::effects::Init{});
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    crucible::safety::Tagged<crucible::cog::CogIdentity,
                             crucible::safety::source::External>
        external{raw};
    auto result = membership.join(external);
    return result.has_value() ? 0 : 1;
}
