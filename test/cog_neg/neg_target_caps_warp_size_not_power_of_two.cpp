// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for cog::PowerOfTwoLane (GAPS-186 #1211).
//
// Premise: GpuTargetCaps::warp_size and CpuCoreTargetCaps::
// simd_vector_lanes are typed as cog::PowerOfTwoLane =
// safety::Refined<all_of<power_of_two, bounded_above<128>>, uint16_t>.
// The Refined ctor pre-clause refuses values that violate the
// invariant — under semantic=enforce the runtime abort fires
// immediately; in manifestly-constant-evaluated context (the
// static_assert below) the contract violation makes the call non-
// constant per P1494R5, and the static_assert is ill-formed.
//
// Why this is the load-bearing soundness gate:
//
// Hardware schedulers fan out warps via bit-mask shifts on per-lane
// predicate registers; non-power-of-two widths are not representable
// in any documented ISA.  The composite SIMD widths the project
// encounters (8/16/32 NVIDIA + Intel, 64 AMD, 4/8/16 Apple AMX) are
// all powers of two with hard ceilings under 128.  If the type system
// allowed warp_size = 33, downstream occupancy / wave-efficiency
// calculations would silently produce non-integer wave counts that
// MAP-Elites bucketization would routes into a non-existent cell, AND
// the ISA-level lane-mask code path would emit garbage shifts.
//
// Companion fixture: neg_target_caps_caps_for_rejects_psu_rail.cpp
//   * That one tests rejection at the CONCEPT gate — HasCaps<K> refuses
//     non-substrate CogKind atoms (PsuRail / BmcSensor / aggregates).
//     Distinct mismatch class (concept substitution failure).
//   * This one tests rejection at the DATA-INVARIANT gate — the pre
//     clause on PowerOfTwoLane refuses non-power-of-two construction.
//     Distinct mismatch class (precondition contract violation on a
//     Refined wrapper).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "static assertion failed" / "contract" /
// "precondition" / "power_of_two" / "non-constant" / "Refined" /
// "PowerOfTwoLane" / "GAPS-186" pointing at the static_assert call
// site below.

#include <crucible/cog/TargetCaps.h>

namespace cog = crucible::cog;

// Constructing PowerOfTwoLane with the value 33 violates the
// `power_of_two` predicate inside the Refined ctor's pre-clause.  In
// manifestly-constant-evaluated context (the static_assert below), the
// contract violation makes the call not a constant expression, so the
// static_assert is ill-formed — the build fails here.
constexpr cog::PowerOfTwoLane BAD_LANE_FIXTURE{std::uint16_t{33}};

static_assert(BAD_LANE_FIXTURE.value() == 33,
    "GAPS-186: cog::PowerOfTwoLane MUST refuse non-power-of-two values "
    "at the Refined precondition contract.  If this static_assert ever "
    "evaluates successfully, GpuTargetCaps::warp_size = 33 would slip "
    "through field construction and corrupt every downstream occupancy "
    "/ wave-efficiency calculation that divides by warp_size.");

int main() { return 0; }
