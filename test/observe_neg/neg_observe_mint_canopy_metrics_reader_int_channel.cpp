// Negative-compile test. This file must fail to compile.
//
// mint_canopy_metrics_reader binds its argument to a metrics channel.
// An int does not bind to that reference, so the call finds no
// function to call.  The context holds Nondet, as the lend of a reader
// share asks, so only the channel fails.

#include <crucible/observe/Metrics.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    int not_a_channel = 0;
    auto reader = crucible::observe::mint_canopy_metrics_reader(ctx, not_a_channel);
    (void)reader;
    return 0;
}
