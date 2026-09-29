#pragma once

// Three-tier chain over how hot the access pattern of a value is: which
// level of the cache hierarchy holds its working set.  It is not where the
// value durably lives, and not what a function is permitted to do.
//
// The hottest residency sits at the top, so `leq(weak, strong)` reads "a
// weaker-heat consumer is satisfied by a stronger-heat provider".  A Hot
// value is admissible everywhere, because it is the most-cached claim
// available.
//
// A structurally identical chain grades a different axis and stays a
// separate type.  The axes are independent, so the grades must never
// collapse into one.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class ResidencyHeatTag : std::uint8_t {
    Cold = 0,  // bottom: L3 or DRAM working-set tail
    Warm = 1,  // L2 working-set body
    Hot = 2,  // top: L1 hottest working-set
};

// A residency nearer the core is the stronger claim.
struct ResidencyHeatLattice
    : EnumChainLattice<ResidencyHeatLattice, ResidencyHeatTag, ClaimOrientation::stronger_is_higher> {
    template <ResidencyHeatTag T>
    struct At : PinnedAt<ResidencyHeatLattice, T> {
        static constexpr ResidencyHeatTag tier = T;
    };
};

namespace residency_heat_tag {
using ColdHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Cold>;
using WarmHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Warm>;
using HotHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Hot>;
}  // namespace residency_heat_tag

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
