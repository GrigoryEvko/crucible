// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The hot foreground context owns no effect, so it holds neither Init nor
// Bg, and mint_cog_mimic refuses it.  Calibration work never runs on the
// latency-critical foreground path.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    const ::fixy::HotFgCtx foreground = ::foundation::effects::testing::foreground();
    cog::CogIdentity identity{};
    identity.uuid = cog::Uuid{0x1ULL, 0x2ULL};
    const auto forged = mimic::mint_cog_mimic<cog::CogKind::Gpu>(foreground, identity, cog::GpuTargetCaps{},
                                                                 cog::OpcodeLatencyTable<cog::CogKind::Gpu>{});
    (void)forged;
    return 0;
}
