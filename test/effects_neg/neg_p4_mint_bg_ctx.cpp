#include <crucible/cntp/P4.h>

int main() {
    namespace cog = crucible::cog;
    namespace eff = crucible::effects;
    namespace p4 = crucible::cntp::p4;

    cog::CogIdentity sw{};
    sw.uuid = cog::Uuid{1, 2};
    sw.kind = cog::CogKind::NvSwitch;

    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::P4);

    p4::P4ProgramSpec spec{};
    auto program = p4::mint_p4_program(eff::BgDrainCtx{}, sw, caps, spec);
    (void)program;
    return 0;
}
