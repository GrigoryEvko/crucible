// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A DOCA deploy plan is minted at initialization.  The background drain
// context owns no Init effect, so the mint refuses it.

#include <crucible/cntp/_wip/Doca.h>
#include <fixy/Ctx.h>

namespace cog = crucible::cog;
namespace doca = crucible::cntp::_wip::doca;

int main() {
    cog::CogIdentity dpu{};
    dpu.uuid = cog::Uuid{1, 2};
    dpu.kind = cog::CogKind::NicCard;

    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Doca);

    const doca::DocaOffloadSpec spec{
        .program_id = *doca::admit_doca_program_id(1),
        .image_bytes = *doca::admit_doca_image_bytes(1),
        .queue_depth = *doca::admit_doca_queue_depth(1),
    };
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto plan = doca::mint_doca_deploy_plan(bg, dpu, caps, spec);
    return plan.has_value() ? 0 : 1;
}
