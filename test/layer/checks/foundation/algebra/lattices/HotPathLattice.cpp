// The compile-time checks of foundation/algebra/lattices/HotPathLattice.h.

#include <foundation/algebra/lattices/HotPathLattice.h>

namespace foundation::algebra::lattices {

namespace detail::hot_path_lattice_self_test {

static_assert(::foundation::reflect::enum_count<HotPathTier> == 3,
              "HotPathTier must hold exactly the three tiers Cold, Warm and Hot.");

static_assert(verify_chain_lattice<HotPathLattice>(), "HotPathLattice: the chain order, the pinned grades or the "
                                                      "reflected names diverged from the HotPathTier enumerator list.");

static_assert(!UnboundedLattice<HotPathLattice>);
static_assert(!Semiring<HotPathLattice>);

static_assert(HotPathLattice::bottom() == HotPathTier::Cold);
static_assert(HotPathLattice::top() == HotPathTier::Hot);

static_assert(HotPathLattice::name() == "HotPathLattice");
static_assert(hot_path_tier::ColdTier::name() == "HotPathLattice::At<Cold>");
static_assert(hot_path_tier::HotTier::name() == "HotPathLattice::At<Hot>");
static_assert(HotPathLattice::At<static_cast<HotPathTier>(255)>::name() == "HotPathLattice::At<?>");

static_assert(hot_path_tier::ColdTier::tier == HotPathTier::Cold);
static_assert(hot_path_tier::HotTier::tier == HotPathTier::Hot);

}  // namespace detail::hot_path_lattice_self_test

}  // namespace foundation::algebra::lattices
