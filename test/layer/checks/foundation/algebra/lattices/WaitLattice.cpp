// The compile-time checks of foundation/algebra/lattices/WaitLattice.h.

#include <foundation/algebra/lattices/WaitLattice.h>

namespace foundation::algebra::lattices {

namespace detail::wait_lattice_self_test {

static_assert(::foundation::reflect::enum_count<WaitStrategy> == 6,
              "WaitStrategy catalog diverged from {Block, Park, AcquireWait, UmwaitC01, BoundedSpin, SpinPause}.  "
              "Confirm intent and update the wait-admission gates.");

static_assert(verify_chain_lattice<WaitLattice>(), "WaitLattice: the chain order, the pinned grades or the reflected "
                                                   "names diverged from the WaitStrategy enumerator list.");

static_assert(!UnboundedLattice<WaitLattice>);
static_assert(!Semiring<WaitLattice>);

static_assert(WaitLattice::bottom() == WaitStrategy::Block);
static_assert(WaitLattice::top() == WaitStrategy::SpinPause);

static_assert(WaitLattice::name() == "WaitLattice");
static_assert(wait_strategy::BlockStrategy::name() == "WaitLattice::At<Block>");
static_assert(wait_strategy::SpinPauseStrategy::name() == "WaitLattice::At<SpinPause>");
static_assert(WaitLattice::At<static_cast<WaitStrategy>(255)>::name() == "WaitLattice::At<?>");

static_assert(wait_strategy::BlockStrategy::strategy == WaitStrategy::Block);
static_assert(wait_strategy::SpinPauseStrategy::strategy == WaitStrategy::SpinPause);

}  // namespace detail::wait_lattice_self_test

}  // namespace foundation::algebra::lattices
