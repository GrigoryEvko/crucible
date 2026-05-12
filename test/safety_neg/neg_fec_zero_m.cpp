// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-117 fixture #2: Reed-Solomon needs at least one parity shard.
// M=0 violates the ReedSolomonShape concept.

#include <crucible/cntp/Fec.h>

int main() {
    (void)sizeof(crucible::cntp::ReedSolomon<4, 0>);
    return 0;
}
