// Negative-compile test. This file must fail to compile.
//
// mint_keeper_metrics_reader has no overload without the metrics
// channel, so a call with only the context finds no function to call.

#include <crucible/observe/Metrics.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto reader = crucible::observe::mint_keeper_metrics_reader(ctx);
    (void)reader;
    return 0;
}
