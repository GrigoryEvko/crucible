// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-196. Calibration is startup/background work, not a
// foreground hot-path operation.

#include <crucible/cog/Calibrate.h>

namespace cog = crucible::cog;
namespace eff = crucible::effects;

int main() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{1, 2};
    id.kind = cog::CogKind::Gpu;
    auto result = cog::calibrate_cog<cog::CogKind::Gpu>(eff::HotFgCtx{}, id);
    return result.has_value() ? 0 : 1;
}
