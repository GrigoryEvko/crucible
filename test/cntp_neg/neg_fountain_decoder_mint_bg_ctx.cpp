// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A fountain decoder is start-up work, so its mint takes a context that
// admits the initialization row.  A background drain context does not,
// and the gate refuses it.

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::BgDrainCtx drain{::foundation::effects::testing::bg()};
    auto decoder = crucible::cntp::mint_fountain_decoder<8, 16>(drain);
    (void)decoder;
    return 0;
}
