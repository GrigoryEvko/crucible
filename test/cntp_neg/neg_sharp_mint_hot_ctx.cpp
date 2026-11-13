// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A SHARP fabric plan is minted at initialization.  The foreground context
// claims no effect, so the mint refuses it.

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

    const ::fixy::HotFgCtx hot = ::foundation::effects::testing::foreground();
    auto result = shp::mint_sharp_fabric_plan(
        hot, sw, caps, shp::admit_sharp_participant_count(8).expect("8 participants is positive"));
    return result.is_ok() ? 0 : 1;
}
