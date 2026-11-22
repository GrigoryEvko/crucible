// Negative compile test.  This file must fail to compile.
//
// Reed-Solomon fixture #2: Reed-Solomon needs at least one parity shard.
// M=0 violates the ReedSolomonShape concept.

#include <crucible/cntp/Fec.h>

int main() {
    (void)sizeof(crucible::cntp::ReedSolomon<4, 0>);
    return 0;
}
