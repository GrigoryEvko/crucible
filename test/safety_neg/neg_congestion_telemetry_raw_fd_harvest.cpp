// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A live TCP_INFO harvest takes an admitted SocketFd.  A raw int does not
// convert to the refined descriptor, so it cannot reach getsockopt.

#include <crucible/topology/CongestionTelemetry.h>

int main() {
    auto sample = crucible::topology::harvest_socket(3);
    (void)sample;
    return 0;
}
