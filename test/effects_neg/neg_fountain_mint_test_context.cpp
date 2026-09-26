// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A fountain encoder is start-up work, so its mint takes a context that
// admits the initialization row.  A test context carries no such effect,
// and the gate refuses it.

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto encoder = crucible::cntp::mint_fountain_encoder<4, 16>(test_ctx);
    (void)encoder;
    return 0;
}
