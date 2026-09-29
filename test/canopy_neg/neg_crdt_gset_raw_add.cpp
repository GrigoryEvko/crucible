// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A local CRDT write must arrive as a LocalWrite, which only
// admit_local_write builds.  A raw value cannot enter the mutation
// boundary.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<int, 8> set;
    (void)set.add(1);
    return 0;
}
