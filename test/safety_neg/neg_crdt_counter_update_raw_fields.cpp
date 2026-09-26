// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A counter update names a replica inside the counter and a positive
// amount.  The two fields are refinements, so raw integers do not build an
// update.  admit_counter_update is the checked door.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::CounterUpdate<4> update{.replica = 9, .amount = 0};
    (void)update;
    return 0;
}
