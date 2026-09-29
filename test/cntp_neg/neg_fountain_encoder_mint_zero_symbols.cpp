// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fountain mint gate holds the shape of the codec as well as the
// context.  The context fits, but an encoder with no source symbol has no
// shape, so the gate refuses the mint.

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto encoder = crucible::cntp::mint_fountain_encoder<0, 16>(init);
    (void)encoder;
    return 0;
}
