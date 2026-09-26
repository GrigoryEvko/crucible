// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A discovery snapshot owns the storage behind the fleet's topology graph,
// so only an initialisation context mints one.  A test context carries no
// Init row and is refused at the requires-clause.

#include <crucible/topology/Discovery.h>

int main() {
    ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto snapshot = crucible::topology::mint_discovery_snapshot<1, 1>(ctx);
    (void)snapshot;
    return 0;
}
