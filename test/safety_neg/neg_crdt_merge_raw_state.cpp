// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-214 fixture #2: received CRDT state must be admitted as
// source::Gossiped before merge.  Raw state is not a merge boundary.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<int, 8> set;
    auto state = set.state();
    (void)set.merge(state);
    return 0;
}
