// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// SDC detector construction is Init-row only.  A background worker may
// run checks, but it must not mint the detector: BgDrainCtx does not
// satisfy CtxFitsSdcMint.

#include <crucible/observe/SdcDetect.h>

namespace eff = ::fixy;
namespace observe = crucible::observe;

int main() {
    auto detector =
        observe::mint_sdc_detector<eff::BgDrainCtx, 2, 4>(eff::BgDrainCtx{::foundation::effects::testing::bg()});
    (void)detector;
    return 0;
}
