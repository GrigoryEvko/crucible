#include <crucible/cntp/_wip/Doca.h>

int main() {
    namespace cog = crucible::cog;
    namespace doca = crucible::cntp::_wip::doca;
    namespace eff = crucible::effects;

    cog::CogIdentity dpu{};
    dpu.uuid = cog::Uuid{1, 2};
    dpu.kind = cog::CogKind::NicCard;

    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Doca);

    doca::DocaOffloadSpec spec{};
    auto plan = doca::mint_doca_deploy_plan(
        eff::BgDrainCtx{}, dpu, caps, spec);
    (void)plan;
    return 0;
}
