#pragma once

// Three-tier chain over where a persisted value lives, and so how fast
// it recovers.  Not how hot its access pattern is, and not what a
// function is permitted to do.
//
// The fastest-recovering tier sits at the top, so `leq(weak, strong)`
// reads "a weaker-durability consumer is satisfied by a
// stronger-durability provider".  A Hot value is admissible everywhere.
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

enum class CipherTierTag : std::uint8_t {
    Cold = 0,  // bottom: durable blob storage, slowest recovery
    Warm = 1,  // node-local disk, survives the process but not the disk
    Hot = 2,  // top: a peer node's RAM, fastest recovery
};

// A faster recovery is the stronger claim.
struct CipherTierLattice : EnumChainLattice<CipherTierLattice, CipherTierTag, ClaimOrientation::stronger_is_higher> {
    template <CipherTierTag T>
    struct At : PinnedAt<CipherTierLattice, T> {
        static constexpr CipherTierTag tier = T;
    };
};

namespace cipher_tier_tag {
using ColdTier = CipherTierLattice::At<CipherTierTag::Cold>;
using WarmTier = CipherTierLattice::At<CipherTierTag::Warm>;
using HotTier = CipherTierLattice::At<CipherTierTag::Hot>;
}  // namespace cipher_tier_tag

namespace detail::cipher_tier_lattice_self_test {

static_assert(::foundation::reflect::enum_count<CipherTierTag> == 3,
              "CipherTierTag must hold exactly the three tiers Cold, Warm and Hot.");

static_assert(verify_chain_lattice<CipherTierLattice>(),
              "CipherTierLattice: the chain order, the pinned grades or the "
              "reflected names diverged from the CipherTierTag enumerator list.");

static_assert(!UnboundedLattice<CipherTierLattice>);
static_assert(!Semiring<CipherTierLattice>);

static_assert(CipherTierLattice::bottom() == CipherTierTag::Cold);
static_assert(CipherTierLattice::top() == CipherTierTag::Hot);

static_assert(CipherTierLattice::name() == "CipherTierLattice");
static_assert(cipher_tier_tag::ColdTier::name() == "CipherTierLattice::At<Cold>");
static_assert(cipher_tier_tag::HotTier::name() == "CipherTierLattice::At<Hot>");
static_assert(CipherTierLattice::At<static_cast<CipherTierTag>(255)>::name() == "CipherTierLattice::At<?>");

static_assert(cipher_tier_tag::ColdTier::tier == CipherTierTag::Cold);
static_assert(cipher_tier_tag::HotTier::tier == CipherTierTag::Hot);

}  // namespace detail::cipher_tier_lattice_self_test

}  // namespace foundation::algebra::lattices
