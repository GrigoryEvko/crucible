// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The snapshot's constructor is private, so mint_discovery_snapshot is the
// only door.  A snapshot built directly would skip the initialisation
// context that the mint requires.

#include <crucible/topology/Discovery.h>

int main() {
    crucible::topology::DefaultDiscoverySnapshot snapshot{};
    (void)snapshot;
    return 0;
}
