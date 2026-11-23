// Negative-compile test. This file must fail to compile.
//
// with_keeper_metrics_reader has no overload without the metrics channel, so
// a call with only the context and the body finds no function to call.

#include <crucible/observe/Metrics.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    (void)crucible::observe::with_keeper_metrics_reader(ctx,
                                                        [](auto reader) noexcept -> void { (void)reader.version(); });
    return 0;
}
