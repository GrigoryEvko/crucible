// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A packet view takes a timestamp tagged as coming from PTP.  A raw
// nanosecond count does not convert to it.

#include <crucible/topology/Ptp.h>

#include <cstddef>
#include <span>

int main() {
    std::byte b{0};
    auto packet = crucible::topology::timestamp_packet_view(std::span<const std::byte>{&b, 1}, 42u, 1);
    (void)packet;
    return 0;
}
