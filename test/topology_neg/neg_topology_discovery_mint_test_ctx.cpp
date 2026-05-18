// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-111 fixture #1: Discovery snapshots own structural topology storage.
// Production snapshots are Init-owned; Test contexts cannot mint them.

#include <crucible/topology/Discovery.h>

int main() {
    auto snapshot = crucible::topology::mint_discovery_snapshot<1, 1>(
        crucible::effects::testing::test());
    (void)snapshot;
    return 0;
}
