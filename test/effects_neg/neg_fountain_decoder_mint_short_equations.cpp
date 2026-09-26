// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A decoder needs one equation slot for each source symbol.  The context
// fits, but seven slots for eight symbols break the shape, and the gate
// refuses the mint.

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto decoder = crucible::cntp::mint_fountain_decoder<8, 16, 7>(init);
    (void)decoder;
    return 0;
}
