// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-129. Raw nanosecond values cannot pass through
// APIs that require source::Ptp timestamp provenance.

#include <crucible/topology/Ptp.h>

int main() {
    std::byte b{0};
    auto packet = crucible::topology::timestamp_packet_view(
        std::span<const std::byte>{&b, 1}, 42u, 1);
    (void)packet;
    return 0;
}
