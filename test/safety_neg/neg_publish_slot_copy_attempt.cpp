// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PublishSlot<T> owns an atomic publication cell.  Copying it would
// duplicate the channel identity and split observers across two cells.

#include <crucible/handles/PublishOnce.h>

int main() {
    crucible::safety::PublishSlot<int> a;
    crucible::safety::PublishSlot<int> b{a};
    (void)b;
    return 0;
}
