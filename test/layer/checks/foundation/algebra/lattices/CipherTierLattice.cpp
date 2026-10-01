// The compile-time checks of foundation/algebra/lattices/CipherTierLattice.h.

#include <foundation/algebra/lattices/CipherTierLattice.h>

namespace foundation::algebra::lattices {

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
