// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PublishSlot<T> carries the same pointee-not-pointer contract as
// PublishOnce, for the same reason: the slot adds the star itself.

#include <crucible/handles/PublishOnce.h>

struct RegionNode;

int main() {
    crucible::safety::PublishSlot<RegionNode*> slot;
    (void)slot;
    return 0;
}
