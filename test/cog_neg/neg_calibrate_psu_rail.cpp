// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-196. Calibration is defined only for substrate Cogs
// with TargetCaps + OpcodeLatencyTable bindings; a PSU rail has neither.

#include <crucible/cog/Calibrate.h>

namespace cog = crucible::cog;
namespace eff = crucible::effects;

int main() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{1, 2};
    id.kind = cog::CogKind::PsuRail;
    auto result = cog::calibrate_cog<cog::CogKind::PsuRail>(
        eff::ColdInitCtx{}, id);
    return result.has_value() ? 0 : 1;
}
