// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for cog::PowerOfTwoLane.
//
// GpuTargetCaps::warp_size and CpuCoreTargetCaps::simd_vector_lanes are
// cog::PowerOfTwoLane, a fixy::Refined over the named predicate
// cog::power_of_two_lane: a power of two no larger than 128.  The only
// checked door is fixy::mint_refined, and it evaluates the predicate on
// the value before the value enters the wrapper.  In a constant
// evaluation a false predicate makes the call non-constant, so the
// constexpr variable below does not compile.
//
// Why this gate carries weight: a scheduler fans warps out through bit
// masks on per-lane predicate registers, and no shipped ISA has a lane
// count that is not a power of two.  A warp size of 33 would give a
// fractional wave count to every occupancy calculation downstream.
//
// Companion fixture: neg_target_caps_caps_for_rejects_psu_rail.cpp
// refuses a kind with no schema at the HasCaps concept.  This one
// refuses a value at the refinement predicate.  The two mismatch classes
// are different.

#include <crucible/cog/TargetCaps.h>

namespace cog = crucible::cog;

constexpr cog::PowerOfTwoLane BAD_LANE_FIXTURE = ::fixy::mint_refined<cog::power_of_two_lane>(std::uint16_t{33});

int main() { return 0; }
