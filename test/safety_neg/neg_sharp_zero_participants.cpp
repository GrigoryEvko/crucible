// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A SHARP fabric plan has at least one participant.  The checked mint of
// the alias refuses zero in a constant evaluation.

#include <crucible/cntp/_wip/Sharp.h>

#include <cstdint>

namespace shp = crucible::cntp::_wip::sharp;

constexpr shp::SharpParticipantCount bad_count = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{0});

int main() { return static_cast<int>(bad_count.value()); }
