// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A slot hash must agree between peers and must place equal values in one
// slot.  A floating-point value fails the second: -0.0 and +0.0 compare
// equal but hash apart, and a NaN never compares equal to itself.  The set
// refuses the value type.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<double, 8> set;
    (void)set;
    return 0;
}
