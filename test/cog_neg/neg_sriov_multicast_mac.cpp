// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A virtual function takes a unicast address. The checked mint of an
// address stops the constant evaluation at a multicast address.

#include <crucible/cog/SrIov.h>

namespace sriov = crucible::cog::sriov;

constexpr sriov::VfMacAddress bad_mac =
    ::fixy::mint_refined<sriov::vf_mac_valid>(sriov::MacAddress{{0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u}});

int main() { return static_cast<int>(bad_mac.value().bytes[0]); }
