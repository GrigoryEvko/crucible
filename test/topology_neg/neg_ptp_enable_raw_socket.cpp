// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Socket timestamping takes an admitted SocketFd.  A raw int does not
// convert to it, so an unchecked descriptor cannot reach setsockopt.

#include <crucible/topology/Ptp.h>

int main() {
    auto enabled = crucible::topology::enable_socket_timestamping(3);
    (void)enabled;
    return 0;
}
