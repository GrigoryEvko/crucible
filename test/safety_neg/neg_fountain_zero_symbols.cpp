// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-119 fixture #1: LT fountain coding needs at least one source
// symbol.  SourceSymbols=0 violates the FountainShape concept before
// any encoder object exists.

#include <crucible/cntp/Fountain.h>

int main() {
    (void)sizeof(crucible::cntp::FountainEncoder<0, 16>);
    return 0;
}
