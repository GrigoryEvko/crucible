// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-117 fixture #1: Reed-Solomon needs at least one data shard.
// K=0 violates the ReedSolomonShape concept before any object exists.

#include <crucible/cntp/Fec.h>

int main() {
    (void)sizeof(crucible::cntp::ReedSolomon<0, 2>);
    return 0;
}
