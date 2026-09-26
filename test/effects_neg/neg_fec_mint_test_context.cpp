// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_reed_solomon is an initialization factory.  A Test context cannot
// stand in for the Init context.

#include <crucible/cntp/Fec.h>

int main() {
    auto rs = crucible::cntp::mint_reed_solomon<4, 2>(::foundation::effects::testing::test());
    (void)rs;
    return 0;
}
