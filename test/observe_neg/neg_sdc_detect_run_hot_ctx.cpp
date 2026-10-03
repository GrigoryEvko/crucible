// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Redundant checks mutate bounded SDC event state, so run_with_redundancy
// needs a background row.  The hot foreground context does not satisfy
// CtxFitsSdcRun.  A foreground context is built only from the producer
// claim, so the call names one in an unevaluated operand.  The size of
// the call is its one use, so the refused call is the only error.

#include <crucible/observe/SdcDetect.h>

#include <utility>

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace observe = crucible::observe;

int main() {
    auto detector =
        observe::mint_sdc_detector<eff::ColdInitCtx, 2, 4>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    static_cast<void>(sizeof(detector.run_with_redundancy(std::declval<eff::HotFgCtx const&>(),
                                                          [](cog::CogIdentity const&) noexcept { return 1u; })));
    return 0;
}
