// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-192. Ring sizes are power-of-two values in
// [256, 8192]; direct construction with 1000 must be rejected at the
// refined boundary.

#include <crucible/cog/NicConfig.h>

namespace nic = crucible::cog::nic;

constexpr nic::NicRingSize bad_ring{std::uint16_t{1000}};

int main() { return bad_ring.value(); }
