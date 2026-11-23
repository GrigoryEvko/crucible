// Negative compile test.  This file must fail to compile.
//
// mint_swmr_reader asks for a channel of the SWMR shape.  An int is not
// a channel, so the mint refuses it.  The context holds Nondet, so only the
// shape of the channel fails.
//
// Expected diagnostic: SwmrSessionSurface is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

int main() {
    namespace ses = ::fixy::channels::swmr_session;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    int not_a_channel = 0;
    auto reader = ses::mint_swmr_reader<int>(ctx, not_a_channel);
    (void)reader;
    return 0;
}
