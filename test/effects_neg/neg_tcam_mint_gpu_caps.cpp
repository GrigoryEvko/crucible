// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Only a NIC port and a switch carry a TCAM, so the mint gate names the
// two caps types.  The context fits, but GPU caps name no TCAM, and the
// gate refuses the mint.

#include <crucible/cntp/Tcam.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace cog = crucible::cog;
    namespace tcam = crucible::cntp::tcam;

    cog::CogIdentity gpu{};
    gpu.uuid = cog::Uuid{1, 2};
    gpu.kind = cog::CogKind::Gpu;

    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto table = tcam::mint_tcam_table(init, gpu, cog::GpuTargetCaps{}, tcam::admit_tcam_entries(1).value());
    (void)table;
    return 0;
}
