// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A CogMimic is minted at calibration time (Init) or during background
// recalibration (Bg), and CtxFitsCogMimic admits a context only when it
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
#include <foundation/effects/Ctx.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

template <::foundation::effects::IsExecCtx Ctx, cog::CogKind K>
    requires mimic::CtxFitsCogMimic<Ctx, K>
constexpr int prepare_cog_mimic_for_test() noexcept {
    return 1;
}

static_assert(prepare_cog_mimic_for_test<::fixy::TestRunnerCtx, cog::CogKind::Gpu>() == 1,
              "CtxFitsCogMimic must refuse a context that owns neither Init nor Bg.");

int main() { return 0; }
