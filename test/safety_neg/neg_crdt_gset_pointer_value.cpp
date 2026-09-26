// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A slot hash must agree between peers.  A pointer hashes an address, and
// an address differs between processes, so two peers would place one value
// in different slots.  The set refuses the value type.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<int*, 8> set;
    (void)set;
    return 0;
}
