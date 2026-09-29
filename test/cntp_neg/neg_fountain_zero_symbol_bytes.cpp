// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-119 fixture #2: each source symbol must carry at least one byte.
// SymbolBytes=0 violates the FountainShape concept.

#include <crucible/cntp/Fountain.h>

int main() {
    (void)sizeof(crucible::cntp::FountainEncoder<4, 0>);
    return 0;
}
