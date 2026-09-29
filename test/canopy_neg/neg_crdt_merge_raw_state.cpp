// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Received CRDT state must be admitted as gossiped state before a merge.
// Raw state does not cross the merge boundary.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<int, 8> set;
    auto state = set.state();
    (void)set.merge(state);
    return 0;
}
