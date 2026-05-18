// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-119 fixture #3: fountain encoder minting is initialization
// authority.  A Test context cannot stand in for effects::Init.

#include <crucible/cntp/Fountain.h>

int main() {
    auto encoder = crucible::cntp::mint_fountain_encoder<4, 16>(
        crucible::effects::testing::test());
    (void)encoder;
    return 0;
}
