// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The netdev parser takes text under the External source tag.  A raw
// string does not convert to it, so host text crosses into the parser
// only through tag_external_telemetry_text.

#include <crucible/topology/Telemetry.h>

int main() {
    auto parsed = crucible::topology::parse_netdev_counters("rx_bytes: 1\n");
    (void)parsed;
    return 0;
}
