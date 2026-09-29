// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Calibration is defined only for a Cog kind that has both a TargetCaps
// schema and an opcode latency table. A PSU rail has neither, so the
// kind gate refuses it before the context is looked at.

#include <crucible/cog/Calibrate.h>

#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace cog = crucible::cog;

int main() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{1, 2};
    id.kind = cog::CogKind::PsuRail;
    auto result =
        cog::calibrate_cog<cog::CogKind::PsuRail>(::fixy::ColdInitCtx{::foundation::effects::testing::init()}, id);
    return result.has_value() ? 0 : 1;
}
