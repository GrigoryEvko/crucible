// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-133. SHARP fabric context minting is Init-row
// work and cannot run in a background drain context.

#include <crucible/cntp/Sharp.h>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace shp = crucible::cntp::sharp;

int main() {
    cog::CogIdentity sw{};
    sw.uuid = cog::Uuid{1, 2};
    sw.kind = cog::CogKind::NvSwitch;
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Sharp);

    auto result = shp::mint_sharp_fabric_plan(
        eff::BgDrainCtx{}, sw, caps,
        *shp::admit_sharp_participant_count(8));
    return result.has_value() ? 0 : 1;
}
