// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-117 fixture #3: GF(2^8) has only 256 shard coordinates.
// K+M above 256 is rejected by ReedSolomonShape.

#include <crucible/cntp/Fec.h>

int main() {
    (void)sizeof(crucible::cntp::ReedSolomon<255, 2>);
    return 0;
}
