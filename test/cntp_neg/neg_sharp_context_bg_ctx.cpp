// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A SHARP context is minted at initialization, as its plan is.  The
// background drain context owns no Init effect, so the mint refuses it.

#include <crucible/cntp/_wip/Sharp.h>
#include <fixy/Ctx.h>

namespace cog = crucible::cog;
namespace shp = crucible::cntp::_wip::sharp;

int main() {
    cog::CogIdentity sw{};
    sw.uuid = cog::Uuid{1, 2};
    sw.kind = cog::CogKind::NvSwitch;
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Sharp);

    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto plan = shp::mint_sharp_fabric_plan(init, sw, caps, *shp::admit_sharp_participant_count(8), true, true);
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto context = shp::mint_sharp_context(bg, *plan);
    return context.has_value() ? 0 : 1;
}
