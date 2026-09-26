// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Only an Init-row context mints the bounded asymmetric-failure detector.

#include <crucible/topology/AsymmetricFailure.h>

int main() {
    auto detector = crucible::topology::mint_asymmetric_failure_detector<::fixy::BgDrainCtx, 2>(
        ::fixy::BgDrainCtx{::foundation::effects::testing::bg()});
    (void)detector;
    return 0;
}
