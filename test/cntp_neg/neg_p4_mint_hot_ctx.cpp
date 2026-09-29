// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A P4 program is minted at initialization.  The foreground context claims
// no effect, so the mint refuses it.

#include <crucible/cntp/_wip/P4.h>
#include <fixy/Ctx.h>

namespace cog = crucible::cog;
namespace p4 = crucible::cntp::_wip::p4;

int main() {
    cog::CogIdentity sw{};
    sw.uuid = cog::Uuid{1, 2};
    sw.kind = cog::CogKind::NvSwitch;

    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::P4);

    const p4::P4ProgramSpec spec{
        .program_id = *p4::admit_p4_program_id(1),
        .source_bytes = *p4::admit_p4_source_bytes(1),
        .budget = *p4::admit_p4_resource_budget(1, 1, 1),
    };
    const ::fixy::HotFgCtx hot = ::foundation::effects::testing::foreground();
    auto program = p4::mint_p4_program(hot, sw, caps, spec);
    return program.has_value() ? 0 : 1;
}
