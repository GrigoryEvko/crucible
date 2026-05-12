// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 for GAPS-180. Redundant checks mutate bounded SDC event
// state and therefore require a background row, not the hot foreground row.

#include <crucible/observe/SdcDetect.h>

namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace observe = crucible::observe;

int main() {
    auto detector = observe::mint_sdc_detector<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{});
    auto result = detector.run_with_redundancy(
        effects::HotFgCtx{}, [](cog::CogIdentity const&) noexcept {
            return 1u;
        });
    (void)result;
    return 0;
}
