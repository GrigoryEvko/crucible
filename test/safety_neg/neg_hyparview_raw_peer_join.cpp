#include <crucible/canopy/HyParView.h>

// Provokes the join provenance gate: raw CogIdentity cannot enter the active
// view until admit_hyparview_peer tags it as source::HyParView.
int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(
        crucible::effects::Init{});
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto result = membership.join(raw);
    return result.has_value() ? 0 : 1;
}
