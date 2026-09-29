// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for cog::caps_for / cog::HasCaps.
//
// Premise: caps_for<K>::type is the kind-to-schema binding metafunction
// for substrate Cogs that publish capability schemas (Gpu, CpuCore,
// CpuSocket, NicPort, NvSwitch, DramChannel — six specialisations).
// Non-schedulable Cogs (PsuRail / BmcSensor / OpticalTransceiver /
// PcieLaneGroup / NvmeNamespace / and the L2..L7 aggregates) do NOT
// publish a TargetCaps schema — there is nothing to schedule on a
// power-supply rail or a thermal sensor; resource budgeting and Mimic
// instances are meaningless concepts for them.
//
// HasCaps<K> is the load-bearing soundness gate that downstream
// templates (a mint_cog_mimic factory, the FitsCog<Row,K> gate, the
// OpcodeLatencyTable<K> table of each Cog) constrain on so
// the type system rejects the misuse "publish a CogMimic for a power
// rail" at template-substitution time.
//
// Why this is the load-bearing soundness gate:
//
// Without this gate, a future mint_cog_mimic factory that takes any CogKind
// would silently produce a PsuRailMimic — a Mimic stub bound to a Cog
// that has no compute substrate, no opcode latency table, no resource
// budgets.  Subsequent calls into that stub would either hit a bare
// `caps_for<CogKind::PsuRail>` resolution failure deep inside the
// kernel-emit pipeline (a confusing "incomplete type" error far from
// the source of the bug), or — worse — fall through to a default
// branch and produce a no-op stub that silently drops scheduled work.
//
// Companion fixture: neg_target_caps_warp_size_not_power_of_two.cpp
//   * That one tests rejection at the DATA-INVARIANT gate — the
//     PowerOfTwoLane Refined alias refuses warp_size = 33 at field
//     construction.  Distinct mismatch class (precondition contract
//     violation on a Refined wrapper).
//   * This one tests rejection at the CONCEPT gate — HasCaps<K> refuses
//     non-substrate CogKind atoms at template substitution.  Distinct
//     mismatch class (concept substitution failure on a constrained
//     template parameter).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: no matching function for the call of
// lookup_caps<CogKind::PsuRail>() below, because the satisfaction of
// HasCaps<K> fails for K = CogKind::PsuRail.

#include <crucible/cog/TargetCaps.h>

namespace cog = crucible::cog;

// Mock of the shape of a future mint_cog_mimic factory, an
// OpcodeLatencyTable or a FitsCog consumer: a function
// templated on a CogKind value, constrained on HasCaps.  Calling it
// with a CogKind that has no schema fails the concept gate at
// substitution time.
template <cog::CogKind K>
    requires cog::HasCaps<K>
constexpr int lookup_caps() noexcept {
    return 1;
}

// CogKind::PsuRail is a power-supply rail atom — no schedulable
// workload, no TargetCaps specialisation.  HasCaps<PsuRail> is false,
// so the requires-clause refuses the substitution and the build fails
// here at the call site.
static_assert(lookup_caps<cog::CogKind::PsuRail>() == 1,
              "cog::HasCaps concept MUST refuse non-substrate CogKind "
              "values.  If this static_assert ever evaluates, a future "
              "mint_cog_mimic factory would accept PsuRail as a target and "
              "produce a Mimic stub bound to a Cog with no compute substrate — "
              "Cog-substrate-binding partition defense compromised.");

int main() { return 0; }
