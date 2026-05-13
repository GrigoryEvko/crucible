// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-147. A physical NIC partition must request at
// least one VF; zero is the disabled state and is handled by disable().

#include <crucible/cog/SrIov.h>

namespace sriov = crucible::cog::sriov;

constexpr sriov::VfCount bad_count{std::uint16_t{0}};

int main() {
    return static_cast<int>(bad_count.value());
}
