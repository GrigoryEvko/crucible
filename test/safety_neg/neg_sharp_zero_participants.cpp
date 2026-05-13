// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-133. A SHARP fabric plan cannot have an empty
// participant set; zero is rejected at the refinement boundary.

#include <crucible/cntp/_wip/Sharp.h>

namespace shp = crucible::cntp::_wip::sharp;

constexpr shp::SharpParticipantCount bad_count{std::uint16_t{0}};

int main() {
    return static_cast<int>(bad_count.value());
}
