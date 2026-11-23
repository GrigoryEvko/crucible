// Negative compile test.  This file must fail to compile.
//
// The reader door with_reader asks for a session of the SWMR shape.  An int
// is not a session, so the door refuses it.  The context holds Nondet, so
// only the shape of the session fails.
//
// Expected diagnostic: SwmrSessionSurface is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>

int main() {
    namespace ses = ::fixy::channels::swmr_session;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    int not_a_channel = 0;
    auto lent = ses::with_reader(ctx, not_a_channel, [](auto reader) noexcept { return reader.version(); });
    return lent.is_some() ? 0 : 1;
}
