// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A packet view takes a timestamp tagged as coming from PTP.  A raw
// nanosecond count does not convert to it.

#include <crucible/topology/Ptp.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<std::byte, 1> const bytes{std::byte{0}};
    auto packet = crucible::topology::timestamp_packet_view(bytes.view(), 42u, 1);
    (void)packet;
    return 0;
}
