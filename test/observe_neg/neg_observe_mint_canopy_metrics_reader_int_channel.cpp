// Negative-compile test. This file must fail to compile.
//
// with_canopy_metrics_reader binds its second argument to a metrics channel.
// An int does not bind to that reference, so the call finds no function to
// call.  The context holds Nondet, as the lend of a reader share asks, and
// the body takes the reader, so only the channel fails.

#include <crucible/observe/Metrics.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    int not_a_channel = 0;
    (void)crucible::observe::with_canopy_metrics_reader(ctx, not_a_channel,
                                                        [](auto reader) noexcept -> void { (void)reader.version(); });
    return 0;
}
