// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-147. VF MAC addresses must be non-zero unicast
// addresses; multicast MACs cannot be assigned as VF identities.

#include <crucible/cog/SrIov.h>

namespace sriov = crucible::cog::sriov;

constexpr sriov::VfMacAddress bad_mac{
    sriov::MacAddress{{0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u}}};

int main() {
    return static_cast<int>(bad_mac.value().bytes[0]);
}
