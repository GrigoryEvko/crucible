// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A CogMimic is minted at calibration time (Init) or during background
// recalibration (Bg), and mint_cog_mimic admits a context only when it
// owns one of the two.  A test runner context owns neither, so a fixture
// worker cannot bind a CogMimic to an identity that lives only as long as
// the fixture.  When that storage unwinds, a CogMimic still holding the
// identity pointer would read freed memory.
//
// The companion fixture neg_cog_mimic_non_substrate.cpp refuses at the
// substrate-family conjunct.  This one refuses at the context-row
// conjunct, a distinct mismatch class.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    const ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    cog::CogIdentity identity{};
    identity.uuid = cog::Uuid{0x1ULL, 0x2ULL};
    const auto forged = mimic::mint_cog_mimic<cog::CogKind::Gpu>(test_ctx, identity, cog::GpuTargetCaps{},
                                                                 cog::OpcodeLatencyTable<cog::CogKind::Gpu>{});
    (void)forged;
    return 0;
}
