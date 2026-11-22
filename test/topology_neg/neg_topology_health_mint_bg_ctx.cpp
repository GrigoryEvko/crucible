// Negative-compile test. This file must fail to compile.
//
// Only an Init-row context mints the health scorer.  A background
// context may feed it samples but may not create the policy carrier.

#include <crucible/topology/Health.h>

int main() {
    auto scorer = crucible::topology::mint_topology_health<::fixy::BgDrainCtx, 2>(
        ::fixy::BgDrainCtx{::foundation::effects::testing::bg()});
    (void)scorer;
    return 0;
}
