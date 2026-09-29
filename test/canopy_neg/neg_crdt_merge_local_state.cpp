// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The local lane and the gossiped lane are distinct types.  State this
// replica authored, admitted through the local door, cannot stand in for
// received gossip at a merge.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<int, 8> set;
    auto local = crucible::canopy::admit_local_write(set.state());
    (void)set.merge(local);
    return 0;
}
