// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-112 fixture #2: netdev parser input is Tagged<source::External>.
// Raw strings cannot cross the kernel-telemetry trust boundary implicitly.

#include <crucible/topology/Telemetry.h>

int main() {
    auto parsed = crucible::topology::parse_netdev_counters("rx_bytes: 1\n");
    (void)parsed;
    return 0;
}
