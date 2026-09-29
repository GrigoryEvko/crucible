// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A live TCP_INFO harvest takes an admitted SocketFd.  A raw int does not
// convert to the refined descriptor, so it cannot reach getsockopt.  The
// context fits the socket option gate, so the descriptor is the one thing
// the call refuses.

#include <crucible/topology/CongestionTelemetry.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto sample = crucible::topology::harvest_socket(test_ctx, 3);
    (void)sample;
    return 0;
}
