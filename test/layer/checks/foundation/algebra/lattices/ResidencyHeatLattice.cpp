// The compile-time checks of foundation/algebra/lattices/ResidencyHeatLattice.h.

#include <foundation/algebra/lattices/ResidencyHeatLattice.h>

namespace foundation::algebra::lattices {

namespace detail::residency_heat_lattice_self_test {

static_assert(::foundation::reflect::enum_count<ResidencyHeatTag> == 3,
              "ResidencyHeatTag must hold exactly the three tiers Cold, Warm and Hot.");

static_assert(verify_chain_lattice<ResidencyHeatLattice>(),
              "ResidencyHeatLattice: the chain order, the pinned grades or the "
              "reflected names diverged from the ResidencyHeatTag enumerator list.");

static_assert(!UnboundedLattice<ResidencyHeatLattice>);
static_assert(!Semiring<ResidencyHeatLattice>);

static_assert(ResidencyHeatLattice::bottom() == ResidencyHeatTag::Cold);
static_assert(ResidencyHeatLattice::top() == ResidencyHeatTag::Hot);
static_assert(ResidencyHeatLattice::leq(ResidencyHeatTag::Warm, ResidencyHeatTag::Hot));
static_assert(!ResidencyHeatLattice::leq(ResidencyHeatTag::Hot, ResidencyHeatTag::Warm));

static_assert(ResidencyHeatLattice::name() == "ResidencyHeatLattice");
static_assert(residency_heat_tag::ColdHeat::name() == "ResidencyHeatLattice::At<Cold>");
static_assert(residency_heat_tag::HotHeat::name() == "ResidencyHeatLattice::At<Hot>");
static_assert(ResidencyHeatLattice::At<static_cast<ResidencyHeatTag>(255)>::name() == "ResidencyHeatLattice::At<?>");

static_assert(residency_heat_tag::ColdHeat::tier == ResidencyHeatTag::Cold);
static_assert(residency_heat_tag::HotHeat::tier == ResidencyHeatTag::Hot);

}  // namespace detail::residency_heat_lattice_self_test

}  // namespace foundation::algebra::lattices
