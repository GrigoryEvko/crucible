// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A CogMimic borrows its identity.  An identity that is a temporary dies
// at the end of the full expression, and the CogMimic would keep a pointer
// to freed storage.  The mint has a deleted form for a temporary, so the
// call is refused even under a context that may mint.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    const ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    const auto dangling = mimic::mint_cog_mimic<cog::CogKind::Gpu>(
        init, cog::CogIdentity{.uuid = cog::Uuid{0x1ULL, 0x2ULL}}, cog::GpuTargetCaps{},
        cog::OpcodeLatencyTable<cog::CogKind::Gpu>{});
    (void)dangling;
    return 0;
}
