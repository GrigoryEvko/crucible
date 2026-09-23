#include <crucible/topology/AsymmetricFailure.h>

int main() {
    auto detector = crucible::topology::mint_asymmetric_failure_detector<crucible::effects::BgDrainCtx, 2>(
        crucible::effects::BgDrainCtx{::crucible::effects::testing::bg()});
    (void)detector;
    return 0;
}
