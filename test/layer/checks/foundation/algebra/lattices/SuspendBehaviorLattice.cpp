// The compile-time checks of foundation/algebra/lattices/SuspendBehaviorLattice.h.

#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>

namespace foundation::algebra::lattices {

namespace detail::suspend_behavior_lattice_self_test {

static_assert(::foundation::reflect::enum_count<SuspendBehavior> == 3,
              "SuspendBehavior catalog diverged from {Unknown, PausesOnSuspend, KeepsTicking}.  A new behavior "
              "needs every composite that names a behavior rechecked.");

static_assert(verify_chain_lattice<SuspendBehaviorLattice>(),
              "SuspendBehaviorLattice: the chain order, the pinned grades or the "
              "reflected names diverged from the SuspendBehavior enumerator list.");

static_assert(!UnboundedLattice<SuspendBehaviorLattice>);
static_assert(!Semiring<SuspendBehaviorLattice>);

static_assert(SuspendBehaviorLattice::bottom() == SuspendBehavior::Unknown);
static_assert(SuspendBehaviorLattice::top() == SuspendBehavior::KeepsTicking);

static_assert(SuspendBehaviorLattice::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking),
              "A boot-clock provider serves a monotonic-clock requirement.");
static_assert(!SuspendBehaviorLattice::leq(SuspendBehavior::KeepsTicking, SuspendBehavior::PausesOnSuspend),
              "A monotonic clock does not satisfy a suspend-inclusive "
              "requirement.  That pairing is the false-healthy deadline reading "
              "this axis forbids.");

static_assert(SuspendBehaviorLattice::name() == "SuspendBehaviorLattice");
static_assert(suspend_behavior::UnknownBehavior::name() == "SuspendBehaviorLattice::At<Unknown>");
static_assert(suspend_behavior::KeepsTickingClock::name() == "SuspendBehaviorLattice::At<KeepsTicking>");
static_assert(SuspendBehaviorLattice::At<static_cast<SuspendBehavior>(255)>::name() == "SuspendBehaviorLattice::At<?>");

static_assert(suspend_behavior::UnknownBehavior::behavior == SuspendBehavior::Unknown);
static_assert(suspend_behavior::KeepsTickingClock::behavior == SuspendBehavior::KeepsTicking);

}  // namespace detail::suspend_behavior_lattice_self_test

}  // namespace foundation::algebra::lattices
