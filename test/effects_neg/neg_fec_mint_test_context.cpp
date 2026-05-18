// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-117 fixture #4: mint_reed_solomon is an initialization factory.
// A Test context cannot stand in for effects::Init authority.

#include <crucible/cntp/Fec.h>

int main() {
    auto rs = crucible::cntp::mint_reed_solomon<4, 2>(
        crucible::effects::testing::test());
    (void)rs;
    return 0;
}
