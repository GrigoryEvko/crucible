// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-214 fixture #1: local CRDT writes must carry source::Local.
// Raw values cannot enter the mutation boundary.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<int, 8> set;
    (void)set.add(1);
    return 0;
}
