#pragma once

// Every concrete lattice in one include.  A translation unit that needs
// only one of them includes that header directly and pays less.
//
// Each lattice ships its own self-test: exhaustive over the carrier when
// the carrier is finite, spot-checked at representative witnesses when
// it is not, plus a runtime smoke test whose operands are non-constant.
// Compile-time assertions alone would not exercise the runtime bodies.
//
// There is deliberately no latency lattice and no energy lattice.
// Neither quantity is measurable well enough on the deployment surface
// to grade a type by, so committing one would put obligations into the
// type system that no consumer can honour.  Budgets over bytes, sequence
// length, staleness, consistency and tolerance stay, because those are
// measurable at the granularity the grade claims.

#include <crucible/algebra/_Lattice.h>  // HasLatticeName

#include <cstdint>  // LatticeNameProbeElement::v
#include <functional>  // std::less, for the MonotoneLattice probe instantiation

#include <crucible/algebra/lattices/AffinityLattice.h>
#include <crucible/algebra/lattices/_AllocClassLattice.h>
#include <crucible/algebra/lattices/_BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/BitsBudgetLattice.h>
#include <crucible/algebra/lattices/_BoolLattice.h>
#include <crucible/algebra/lattices/CallShapeLattice.h>
#include <crucible/algebra/lattices/_CipherTierLattice.h>
#include <crucible/algebra/lattices/_ClockSourceLattice.h>
#include <crucible/algebra/lattices/_ConfLattice.h>
#include <crucible/algebra/lattices/ConsistencyLattice.h>
#include <crucible/algebra/lattices/ControlFlowLattice.h>
#include <crucible/algebra/lattices/CrashLattice.h>
#include <crucible/algebra/lattices/_DetSafeLattice.h>
#include <crucible/algebra/lattices/EpochLattice.h>
#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/algebra/lattices/_FractionalLattice.h>
#include <crucible/algebra/lattices/GenerationLattice.h>
#include <crucible/algebra/lattices/GlobalStateLattice.h>
#include <crucible/algebra/lattices/HappensBefore.h>
#include <crucible/algebra/lattices/_HotPathLattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>
#include <crucible/algebra/lattices/JoinPolicyLattice.h>
#include <crucible/algebra/lattices/LifetimeLattice.h>
#include <crucible/algebra/lattices/MemOrderLattice.h>
#include <crucible/algebra/lattices/ProgressLattice.h>
#include <crucible/algebra/lattices/_MonotoneLattice.h>
#include <crucible/algebra/lattices/NumaNodeLattice.h>
#include <crucible/algebra/lattices/PeakBytesLattice.h>
#include <crucible/algebra/lattices/_PinningRequirementLattice.h>
#include <crucible/algebra/lattices/_ProductLattice.h>
#include <crucible/algebra/lattices/_QttSemiring.h>
#include <crucible/algebra/lattices/_RecipeFamilyLattice.h>
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>
#include <crucible/algebra/lattices/SchedulerPolicyLattice.h>
#include <crucible/algebra/lattices/_SeqPrefixLattice.h>
#include <crucible/algebra/lattices/SimdIsaLattice.h>
#include <crucible/algebra/lattices/StackUseLattice.h>
#include <crucible/algebra/lattices/_StalenessSemiring.h>
#include <crucible/algebra/lattices/StdioLattice.h>
#include <crucible/algebra/lattices/_SuspendBehaviorLattice.h>
#include <crucible/algebra/lattices/SyscallFamilyLattice.h>
#include <crucible/algebra/lattices/_ToleranceLattice.h>
#include <crucible/algebra/lattices/_TrustLattice.h>
#include <crucible/algebra/lattices/_VendorLattice.h>
#include <crucible/algebra/lattices/_WaitLattice.h>
#include <crucible/algebra/lattices/WitnessLattice.h>

// The underlying-value pins ride the umbrella so that their assertions
// fire in every translation unit that pulls any lattice at all.
#include <crucible/algebra/lattices/_EnumValuePins.h>

namespace crucible::algebra::lattices {

template <typename... Ls>
struct ProductLattice;

// A lattice with no name() member renders as an "<unnamed lattice>"
// sentinel in every diagnostic that mentions it, which no test sees.
// The pack below is the guard, and it is maintained by hand: adding a
// header to the includes above without adding the lattice here does not
// fire.  The pack is the grep point for that discipline.
//
// A templated lattice's name() does not depend on its arguments, so one
// representative instantiation per template is enough to catch a
// removed member.

namespace detail::lattice_name_coverage {

// Stateless tags that exist only to fill a template parameter.  No
// lattice operation is exercised here.
struct LatticeNameProbeTruePred {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const&) noexcept {
        return true;
    }
};
struct LatticeNameProbeSource {};
struct LatticeNameProbeElement {
    std::uint32_t v = 0;
    [[nodiscard]] constexpr bool operator==(LatticeNameProbeElement const&) const noexcept = default;
};

template <typename... Ls>
[[nodiscard]] consteval bool every_lattice_has_name() noexcept {
    return (HasLatticeName<Ls> && ...);
}

static_assert(
    every_lattice_has_name<
        AffinityLattice, AllocClassLattice, BarrierStrengthLattice, BitsBudgetLattice,
        BoolLattice<LatticeNameProbeTruePred>, CallShapeLattice, CipherTierLattice, ClockSourceLattice, ConfLattice,
        ConsistencyLattice, ControlFlowLattice, CrashLattice, DetSafeLattice, EpochLattice, FpComplexLayoutLattice,
        FpConstantRoundingLattice, FpContractLattice, FpDenormalInputLattice, FpFtzLattice, FpInfPolicyLattice,
        FpLibmPolicyLattice, FpNanPolicyLattice, FpReassociateLattice, FpRoundingLattice, FpTrapMaskLattice,
        FractionalLattice, GenerationLattice, GlobalStateLattice, HappensBeforeLattice<4>, HotPathLattice,
        HwInstructionLattice, LifetimeLattice, MemOrderLattice, MonotoneLattice<int, std::less<int>>, NumaNodeLattice,
        PeakBytesLattice, PinningRequirementLattice, ProductLattice<HotPathLattice, DetSafeLattice>,
        // The binary product above already covers the template
        // structurally.  Pinning the eleven-way form as well guards
        // against an arity refactor slipping past this pack.
        ProductLattice<FpRoundingLattice, FpFtzLattice, FpContractLattice, FpTrapMaskLattice, FpDenormalInputLattice,
                       FpNanPolicyLattice, FpInfPolicyLattice, FpComplexLayoutLattice, FpLibmPolicyLattice,
                       FpReassociateLattice, FpConstantRoundingLattice>,
        ProgressLattice, QttSemiring, RecipeFamilyLattice, ResidencyHeatLattice, SchedulerPolicyLattice,
        SeqPrefixLattice<LatticeNameProbeElement>, SimdIsaLattice, StackUseLattice, StalenessSemiring, StdioLattice,
        SuspendBehaviorLattice, SyscallFamilyLattice, ToleranceLattice, TrustLattice<LatticeNameProbeSource>,
        VendorLattice, WaitLattice, WitnessLattice>(),
    "[Lattice_Missing_Name] At least one shipped lattice does not "
    "satisfy HasLatticeName<L>, so `lattice_name<L>()` returns the "
    "`<unnamed lattice>` sentinel and every diagnostic naming that "
    "lattice degrades silently.  Add `static consteval "
    "std::string_view name() noexcept` to the lattice.");

}  // namespace detail::lattice_name_coverage

}  // namespace crucible::algebra::lattices
