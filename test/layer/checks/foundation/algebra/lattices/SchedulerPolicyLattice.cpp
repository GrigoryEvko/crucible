// The compile-time checks of foundation/algebra/lattices/SchedulerPolicyLattice.h.

#include <foundation/algebra/lattices/SchedulerPolicyLattice.h>

namespace foundation::algebra::lattices {

namespace detail::scheduler_policy_lattice_self_test {

static_assert(::foundation::reflect::enum_count<SchedulerPolicy> == 6,
              "SchedulerPolicy catalog diverged from {Idle, Batch, Other, RoundRobin, Fifo, Deadline}.  A new class "
              "needs the admission thresholds that name a class rechecked.");

// The generic walk covers the declaration order, the exhaustive axioms,
// the reflected names and the shape of every At<policy>.
static_assert(verify_chain_lattice<SchedulerPolicyLattice>(),
              "SchedulerPolicyLattice: the chain order, the pinned grades or the reflected names diverged from "
              "the SchedulerPolicy enumerator list.");

static_assert(!UnboundedLattice<SchedulerPolicyLattice>);
static_assert(!Semiring<SchedulerPolicyLattice>);

static_assert(SchedulerPolicyLattice::bottom() == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::top() == SchedulerPolicy::Deadline);

// The pairs that the admission thresholds read.
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo),
              "A FIFO thread serves a round-robin requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo, SchedulerPolicy::RoundRobin),
              "A round-robin thread does not satisfy a FIFO requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Batch),
              "SCHED_BATCH sits below the Other threshold that a timestamp-counter "
              "read requires.  The kernel may migrate the thread mid-quantum, so a "
              "sched_getcpu pin proves nothing there.");

static_assert(SchedulerPolicyLattice::name() == "SchedulerPolicyLattice");
static_assert(scheduler_policy::FifoClass::name() == "SchedulerPolicyLattice::At<Fifo>");
static_assert(SchedulerPolicyLattice::At<static_cast<SchedulerPolicy>(200)>::name() == "SchedulerPolicyLattice::At<?>");

static_assert(scheduler_policy::IdleClass::policy == SchedulerPolicy::Idle);
static_assert(scheduler_policy::DeadlineClass::policy == SchedulerPolicy::Deadline);

}  // namespace detail::scheduler_policy_lattice_self_test

}  // namespace foundation::algebra::lattices
