// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 for GAPS-180. SDC detector construction is Init-row
// only; a background worker may run checks but must not mint the detector.

#include <crucible/observe/SdcDetect.h>

namespace effects = crucible::effects;
namespace observe = crucible::observe;

int main() {
    auto detector = observe::mint_sdc_detector<effects::BgDrainCtx, 2, 4>(
        effects::BgDrainCtx{});
    (void)detector;
    return 0;
}
