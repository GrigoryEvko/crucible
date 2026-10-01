// The compile-time checks of foundation/algebra/lattices/LifetimeLattice.h.

#include <foundation/algebra/lattices/LifetimeLattice.h>

namespace foundation::algebra::lattices {

namespace detail::lifetime_lattice_self_test {

static_assert(::foundation::reflect::enum_count<Lifetime> == 3,
              "Lifetime must hold exactly the three scopes PER_REQUEST, PER_PROGRAM and PER_FLEET.");

static_assert(verify_chain_lattice<LifetimeLattice>(),
              "LifetimeLattice: the chain order, the pinned grades or the reflected "
              "names diverged from the Lifetime enumerator list.");

static_assert(!UnboundedLattice<LifetimeLattice>);
static_assert(!Semiring<LifetimeLattice>);

static_assert(LifetimeLattice::bottom() == Lifetime::PER_REQUEST);
static_assert(LifetimeLattice::top() == Lifetime::PER_FLEET);

static_assert(LifetimeLattice::name() == "LifetimeLattice");
static_assert(LifetimeLattice::At<Lifetime::PER_REQUEST>::name() == "LifetimeLattice::At<PER_REQUEST>");
static_assert(LifetimeLattice::At<Lifetime::PER_FLEET>::name() == "LifetimeLattice::At<PER_FLEET>");
static_assert(LifetimeLattice::At<static_cast<Lifetime>(255)>::name() == "LifetimeLattice::At<?>");

static_assert(lifetime::PerRequestTier::scope == Lifetime::PER_REQUEST);
static_assert(lifetime::PerFleetTier::scope == Lifetime::PER_FLEET);

}  // namespace detail::lifetime_lattice_self_test

}  // namespace foundation::algebra::lattices
