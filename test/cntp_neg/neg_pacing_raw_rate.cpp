#include <crucible/cntp/Pacing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

// A SO_MAX_PACING_RATE write takes an admitted positive pacing rate.  A
// raw integer cannot drive the socket call.  The context fits the socket
// option gate, so the rate is the one thing the call refuses.

int main() {
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto fd = crucible::cntp::admit_socket_fd(0).value();
    auto result = crucible::cntp::set_socket_pacing_rate(test_ctx, fd, 1000000ULL);
    (void)result;
    return 0;
}
