// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// GAPS-129 boundary fixture. Socket timestamping setup requires an
// admitted cntp::SocketFd; raw integers cannot reach setsockopt.

#include <crucible/topology/Ptp.h>

int main() {
    auto enabled = crucible::topology::enable_socket_timestamping(3);
    (void)enabled;
    return 0;
}
